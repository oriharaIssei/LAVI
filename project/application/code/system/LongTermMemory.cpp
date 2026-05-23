#include "LongTermMemory.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static const char* SafeText(const unsigned char* p) {
    return p ? reinterpret_cast<const char*>(p) : "";
}

LongTermMemory::LongTermMemory() {}

LongTermMemory::~LongTermMemory() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void LongTermMemory::ExecSql(const char* sql) const {
    if (!db_) return;
    char* err = nullptr;
    sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

bool LongTermMemory::InitDatabase() {
    std::string dbPath = storagePath_ + "memory.db";
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }

    ExecSql("PRAGMA journal_mode=WAL");
    ExecSql("PRAGMA synchronous=NORMAL");

    ExecSql(
        "CREATE TABLE IF NOT EXISTS app_daily_stats ("
        "  process_name TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  fg_count INTEGER DEFAULT 0,"
        "  fg_minutes REAL DEFAULT 0,"
        "  bg_count INTEGER DEFAULT 0,"
        "  bg_minutes REAL DEFAULT 0,"
        "  PRIMARY KEY(process_name, date)"
        ")");

    ExecSql(
        "CREATE TABLE IF NOT EXISTS app_transitions ("
        "  from_app TEXT NOT NULL,"
        "  to_app TEXT NOT NULL,"
        "  count INTEGER DEFAULT 0,"
        "  last_time TEXT,"
        "  PRIMARY KEY(from_app, to_app)"
        ")");

    ExecSql(
        "CREATE TABLE IF NOT EXISTS app_launches ("
        "  app TEXT NOT NULL,"
        "  launched_from TEXT NOT NULL,"
        "  count INTEGER DEFAULT 0,"
        "  last_time TEXT,"
        "  PRIMARY KEY(app, launched_from)"
        ")");

    ExecSql(
        "CREATE TABLE IF NOT EXISTS facts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  category TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  learned_at TEXT,"
        "  importance REAL DEFAULT 1.0"
        ")");
    ExecSql("CREATE INDEX IF NOT EXISTS idx_facts_category ON facts(category)");

    return true;
}

void LongTermMemory::UpsertDailyStat(const std::string& processName, const std::string& date,
                                      float fgMinutes, float bgMinutes, bool isForeground) {
    if (!db_) return;
    const char* sql =
        "INSERT INTO app_daily_stats (process_name, date, fg_count, fg_minutes, bg_count, bg_minutes) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6) "
        "ON CONFLICT(process_name, date) DO UPDATE SET "
        "fg_count = fg_count + ?3, fg_minutes = fg_minutes + ?4, "
        "bg_count = bg_count + ?5, bg_minutes = bg_minutes + ?6";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, processName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, isForeground ? 1 : 0);
        sqlite3_bind_double(stmt, 4, isForeground ? fgMinutes : 0.0);
        sqlite3_bind_int(stmt, 5, isForeground ? 0 : 1);
        sqlite3_bind_double(stmt, 6, isForeground ? 0.0 : bgMinutes);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void LongTermMemory::MigrateJsonToSqlite() {
    if (!db_) return;

    auto extractField = [](const std::string& obj, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        auto p = obj.find(search);
        if (p == std::string::npos) return "";
        p += search.size();
        auto e = obj.find('"', p);
        if (e == std::string::npos) return "";
        return obj.substr(p, e - p);
    };
    auto extractInt = [](const std::string& obj, const std::string& key) -> int {
        std::string search = "\"" + key + "\":";
        auto p = obj.find(search);
        if (p == std::string::npos) return 0;
        p += search.size();
        return std::atoi(obj.c_str() + p);
    };
    auto extractFloat = [](const std::string& obj, const std::string& key) -> float {
        std::string search = "\"" + key + "\":";
        auto p = obj.find(search);
        if (p == std::string::npos) return 0.0f;
        p += search.size();
        return std::strtof(obj.c_str() + p, nullptr);
    };

    // daily stats
    std::string dailyPath = storagePath_ + "app_daily_stats.json";
    std::string dailyJson;
    if (LoadJson(dailyPath, dailyJson)) {
        ExecSql("BEGIN TRANSACTION");
        size_t pos = 0;
        while (true) {
            auto s = dailyJson.find('{', pos);
            if (s == std::string::npos) break;
            auto e = dailyJson.find('}', s);
            if (e == std::string::npos) break;
            std::string obj = dailyJson.substr(s, e - s + 1);

            std::string pn = extractField(obj, "processName");
            std::string dt = extractField(obj, "date");
            if (!pn.empty() && !dt.empty()) {
                const char* sql = "INSERT OR REPLACE INTO app_daily_stats VALUES(?1,?2,?3,?4,?5,?6)";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, pn.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, dt.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, 3, extractInt(obj, "fgCount"));
                    sqlite3_bind_double(stmt, 4, extractFloat(obj, "fgMin"));
                    sqlite3_bind_int(stmt, 5, extractInt(obj, "bgCount"));
                    sqlite3_bind_double(stmt, 6, extractFloat(obj, "bgMin"));
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
            pos = e + 1;
        }
        ExecSql("COMMIT");
        fs::rename(dailyPath, dailyPath + ".migrated");
    }

    // transitions
    std::string transPath = storagePath_ + "app_transitions.json";
    std::string transJson;
    if (LoadJson(transPath, transJson)) {
        ExecSql("BEGIN TRANSACTION");
        size_t pos = 0;
        while (true) {
            auto s = transJson.find('{', pos);
            if (s == std::string::npos) break;
            auto e = transJson.find('}', s);
            if (e == std::string::npos) break;
            std::string obj = transJson.substr(s, e - s + 1);

            std::string from = extractField(obj, "from");
            std::string to = extractField(obj, "to");
            if (!from.empty() && !to.empty()) {
                const char* sql = "INSERT OR REPLACE INTO app_transitions VALUES(?1,?2,?3,?4)";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, from.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, to.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, 3, extractInt(obj, "count"));
                    std::string lt = extractField(obj, "lastTime");
                    sqlite3_bind_text(stmt, 4, lt.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
            pos = e + 1;
        }
        ExecSql("COMMIT");
        fs::rename(transPath, transPath + ".migrated");
    }

    // launches
    std::string launchPath = storagePath_ + "app_launches.json";
    std::string launchJson;
    if (LoadJson(launchPath, launchJson)) {
        ExecSql("BEGIN TRANSACTION");
        size_t pos = 0;
        while (true) {
            auto s = launchJson.find('{', pos);
            if (s == std::string::npos) break;
            auto e = launchJson.find('}', s);
            if (e == std::string::npos) break;
            std::string obj = launchJson.substr(s, e - s + 1);

            std::string app = extractField(obj, "app");
            std::string from = extractField(obj, "from");
            if (!app.empty()) {
                const char* sql = "INSERT OR REPLACE INTO app_launches VALUES(?1,?2,?3,?4)";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, app.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, from.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int(stmt, 3, extractInt(obj, "count"));
                    std::string lt = extractField(obj, "lastTime");
                    sqlite3_bind_text(stmt, 4, lt.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
            pos = e + 1;
        }
        ExecSql("COMMIT");
        fs::rename(launchPath, launchPath + ".migrated");
    }

    // facts
    std::string factsPath = storagePath_ + "facts.json";
    std::string factsJson;
    if (LoadJson(factsPath, factsJson)) {
        ExecSql("BEGIN TRANSACTION");
        size_t pos = 0;
        while (true) {
            auto s = factsJson.find('{', pos);
            if (s == std::string::npos) break;
            auto e = factsJson.find('}', s);
            if (e == std::string::npos) break;
            std::string obj = factsJson.substr(s, e - s + 1);

            std::string category = extractField(obj, "category");
            std::string content = extractField(obj, "content");
            if (!content.empty()) {
                const char* sql = "INSERT INTO facts (category, content, learned_at, importance) VALUES(?1,?2,?3,?4)";
                sqlite3_stmt* stmt = nullptr;
                if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
                    std::string la = extractField(obj, "learnedAt");
                    sqlite3_bind_text(stmt, 3, la.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_double(stmt, 4, extractFloat(obj, "importance"));
                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
            }
            pos = e + 1;
        }
        ExecSql("COMMIT");
        fs::rename(factsPath, factsPath + ".migrated");
    }
}

void LongTermMemory::SetStoragePath(const std::string& dirPath) {
    storagePath_ = dirPath;
    if (!storagePath_.empty() && storagePath_.back() != '/' && storagePath_.back() != '\\') {
        storagePath_ += '/';
    }
}

bool LongTermMemory::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (storagePath_.empty()) return false;

    fs::create_directories(storagePath_);

    // Load user profile (JSON)
    std::string profileJson;
    if (LoadJson(storagePath_ + "user_profile.json", profileJson)) {
        auto extractString = [&](const std::string& json, const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            auto pos = json.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = json.find('"', pos);
            if (end == std::string::npos) return "";
            return json.substr(pos, end - pos);
        };

        auto extractArray = [&](const std::string& json, const std::string& key) -> std::vector<std::string> {
            std::vector<std::string> result;
            std::string search = "\"" + key + "\":[";
            auto pos = json.find(search);
            if (pos == std::string::npos) return result;
            pos += search.size();
            auto end = json.find(']', pos);
            if (end == std::string::npos) return result;
            std::string arr = json.substr(pos, end - pos);
            size_t cur = 0;
            while (cur < arr.size()) {
                auto q1 = arr.find('"', cur);
                if (q1 == std::string::npos) break;
                auto q2 = arr.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                result.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                cur = q2 + 1;
            }
            return result;
        };

        userProfile_.name = extractString(profileJson, "name");
        userProfile_.interests = extractArray(profileJson, "interests");
        userProfile_.dislikedTopics = extractArray(profileJson, "dislikedTopics");
        userProfile_.notes = extractString(profileJson, "notes");
    }

    // Load app usage (JSON)
    auto extractField = [](const std::string& obj, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        auto p = obj.find(search);
        if (p == std::string::npos) return "";
        p += search.size();
        auto e = obj.find('"', p);
        if (e == std::string::npos) return "";
        return obj.substr(p, e - p);
    };
    auto extractInt = [](const std::string& obj, const std::string& key) -> int {
        std::string search = "\"" + key + "\":";
        auto p = obj.find(search);
        if (p == std::string::npos) return 0;
        p += search.size();
        return std::atoi(obj.c_str() + p);
    };
    auto extractFloat = [](const std::string& obj, const std::string& key) -> float {
        std::string search = "\"" + key + "\":";
        auto p = obj.find(search);
        if (p == std::string::npos) return 0.0f;
        p += search.size();
        return std::strtof(obj.c_str() + p, nullptr);
    };

    std::string appJson;
    if (LoadJson(storagePath_ + "app_usage.json", appJson)) {
        appUsage_.clear();
        size_t pos = 0;
        while (true) {
            auto objStart = appJson.find('{', pos);
            if (objStart == std::string::npos) break;
            auto objEnd = appJson.find('}', objStart);
            if (objEnd == std::string::npos) break;
            std::string obj = appJson.substr(objStart, objEnd - objStart + 1);

            AppUsageRecord rec;
            rec.processName = extractField(obj, "processName");
            rec.windowTitle = extractField(obj, "windowTitle");
            rec.launchCount = extractInt(obj, "launchCount");
            rec.totalForegroundMinutes = extractFloat(obj, "totalForegroundMinutes");
            if (rec.totalForegroundMinutes == 0.0f) {
                rec.totalForegroundMinutes = extractFloat(obj, "totalMinutes");
            }
            rec.totalBackgroundMinutes = extractFloat(obj, "totalBackgroundMinutes");
            rec.firstSeen = extractField(obj, "firstSeen");
            rec.lastSeen = extractField(obj, "lastSeen");

            {
                auto tp = obj.find("\"tags\":[");
                if (tp != std::string::npos) {
                    tp = obj.find('[', tp);
                    auto te = obj.find(']', tp);
                    if (te != std::string::npos) {
                        std::string arr = obj.substr(tp + 1, te - tp - 1);
                        size_t cur = 0;
                        while (cur < arr.size()) {
                            auto q1 = arr.find('"', cur);
                            if (q1 == std::string::npos) break;
                            auto q2 = arr.find('"', q1 + 1);
                            if (q2 == std::string::npos) break;
                            rec.tags.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                            cur = q2 + 1;
                        }
                    }
                }
            }

            if (!rec.processName.empty()) {
                appUsage_.push_back(std::move(rec));
            }
            pos = objEnd + 1;
        }
    }

    // Load web services (JSON)
    std::string webJson;
    if (LoadJson(storagePath_ + "web_services.json", webJson)) {
        webServices_.clear();
        size_t pos = 0;
        while (true) {
            auto objStart = webJson.find('{', pos);
            if (objStart == std::string::npos) break;
            auto objEnd = webJson.find('}', objStart);
            if (objEnd == std::string::npos) break;
            std::string obj = webJson.substr(objStart, objEnd - objStart + 1);

            WebServiceRecord ws;
            ws.serviceName = extractField(obj, "serviceName");
            ws.browserProcess = extractField(obj, "browser");
            ws.totalMinutes = extractFloat(obj, "totalMinutes");
            ws.visitCount = extractInt(obj, "visitCount");
            ws.firstVisited = extractField(obj, "firstVisited");
            ws.lastVisited = extractField(obj, "lastVisited");

            {
                auto tp = obj.find("\"tags\":[");
                if (tp != std::string::npos) {
                    tp = obj.find('[', tp);
                    auto te = obj.find(']', tp);
                    if (te != std::string::npos) {
                        std::string arr = obj.substr(tp + 1, te - tp - 1);
                        size_t cur = 0;
                        while (cur < arr.size()) {
                            auto q1 = arr.find('"', cur);
                            if (q1 == std::string::npos) break;
                            auto q2 = arr.find('"', q1 + 1);
                            if (q2 == std::string::npos) break;
                            ws.tags.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                            cur = q2 + 1;
                        }
                    }
                }
            }

            if (!ws.serviceName.empty()) {
                webServices_.push_back(std::move(ws));
            }
            pos = objEnd + 1;
        }
    }

    // Initialize SQLite and migrate old JSON data
    InitDatabase();
    MigrateJsonToSqlite();

    return true;
}

bool LongTermMemory::Save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (storagePath_.empty()) return false;

    fs::create_directories(storagePath_);

    // Save user profile (JSON)
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"name\":\"" << userProfile_.name << "\",\n";
        json << "  \"interests\":[";
        for (size_t i = 0; i < userProfile_.interests.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << userProfile_.interests[i] << "\"";
        }
        json << "],\n";
        json << "  \"dislikedTopics\":[";
        for (size_t i = 0; i < userProfile_.dislikedTopics.size(); ++i) {
            if (i > 0) json << ",";
            json << "\"" << userProfile_.dislikedTopics[i] << "\"";
        }
        json << "],\n";
        json << "  \"notes\":\"" << userProfile_.notes << "\"\n";
        json << "}\n";
        SaveJson(storagePath_ + "user_profile.json", json.str());
    }

    // Save app usage (JSON)
    {
        std::ostringstream json;
        json << "[\n";
        for (size_t i = 0; i < appUsage_.size(); ++i) {
            auto& r = appUsage_[i];
            if (i > 0) json << ",\n";
            json << "  {\"processName\":\"" << r.processName
                 << "\",\"windowTitle\":\"" << r.windowTitle
                 << "\",\"tags\":[";
            for (size_t t = 0; t < r.tags.size(); ++t) {
                if (t > 0) json << ",";
                json << "\"" << r.tags[t] << "\"";
            }
            json << "],\"launchCount\":" << r.launchCount
                 << ",\"totalForegroundMinutes\":" << r.totalForegroundMinutes
                 << ",\"totalBackgroundMinutes\":" << r.totalBackgroundMinutes
                 << ",\"firstSeen\":\"" << r.firstSeen
                 << "\",\"lastSeen\":\"" << r.lastSeen << "\"}";
        }
        json << "\n]\n";
        SaveJson(storagePath_ + "app_usage.json", json.str());
    }

    // Save web services (JSON)
    {
        std::ostringstream json;
        json << "[\n";
        for (size_t i = 0; i < webServices_.size(); ++i) {
            auto& ws = webServices_[i];
            if (i > 0) json << ",\n";
            json << "  {\"serviceName\":\"" << ws.serviceName
                 << "\",\"browser\":\"" << ws.browserProcess
                 << "\",\"tags\":[";
            for (size_t t = 0; t < ws.tags.size(); ++t) {
                if (t > 0) json << ",";
                json << "\"" << ws.tags[t] << "\"";
            }
            json << "],\"totalMinutes\":" << ws.totalMinutes
                 << ",\"visitCount\":" << ws.visitCount
                 << ",\"firstVisited\":\"" << ws.firstVisited
                 << "\",\"lastVisited\":\"" << ws.lastVisited << "\"}";
        }
        json << "\n]\n";
        SaveJson(storagePath_ + "web_services.json", json.str());
    }

    return true;
}

void LongTermMemory::RecordAppForeground(const std::string& processName, const std::string& windowTitle, float durationMinutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = GetCurrentTimeString();
    std::string today = now.substr(0, 10);

    auto it = std::find_if(appUsage_.begin(), appUsage_.end(),
        [&](const AppUsageRecord& r) { return r.processName == processName; });

    if (it != appUsage_.end()) {
        it->launchCount++;
        it->totalForegroundMinutes += durationMinutes;
        it->windowTitle = windowTitle;
        it->lastSeen = now;
    } else {
        AppUsageRecord rec;
        rec.processName = processName;
        rec.windowTitle = windowTitle;
        rec.launchCount = 1;
        rec.totalForegroundMinutes = durationMinutes;
        rec.firstSeen = now;
        rec.lastSeen = now;
        appUsage_.push_back(std::move(rec));
    }

    UpsertDailyStat(processName, today, durationMinutes, 0.0f, true);
}

void LongTermMemory::RecordAppBackground(const std::string& processName, const std::string& windowTitle, float durationMinutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = GetCurrentTimeString();
    std::string today = now.substr(0, 10);

    auto it = std::find_if(appUsage_.begin(), appUsage_.end(),
        [&](const AppUsageRecord& r) { return r.processName == processName; });

    if (it != appUsage_.end()) {
        it->totalBackgroundMinutes += durationMinutes;
        it->lastSeen = now;
    } else {
        AppUsageRecord rec;
        rec.processName = processName;
        rec.windowTitle = windowTitle;
        rec.launchCount = 1;
        rec.totalBackgroundMinutes = durationMinutes;
        rec.firstSeen = now;
        rec.lastSeen = now;
        appUsage_.push_back(std::move(rec));
    }

    UpsertDailyStat(processName, today, 0.0f, durationMinutes, false);
}

void LongTermMemory::SetAppTags(const std::string& processName, const std::vector<std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(appUsage_.begin(), appUsage_.end(),
        [&](const AppUsageRecord& r) { return r.processName == processName; });
    if (it != appUsage_.end()) {
        it->tags = tags;
    }
}

AppUsageRecord* LongTermMemory::FindAppRecord(const std::string& processName) {
    auto it = std::find_if(appUsage_.begin(), appUsage_.end(),
        [&](const AppUsageRecord& r) { return r.processName == processName; });
    return it != appUsage_.end() ? &(*it) : nullptr;
}

std::vector<AppDailyStat> LongTermMemory::GetDailyStats(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AppDailyStat> result;
    if (!db_) return result;

    std::string sql = "SELECT process_name, date, fg_count, fg_minutes, bg_count, bg_minutes "
                      "FROM app_daily_stats ORDER BY date DESC";
    if (limit > 0) sql += " LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AppDailyStat ds;
            ds.processName = SafeText(sqlite3_column_text(stmt, 0));
            ds.date = SafeText(sqlite3_column_text(stmt, 1));
            ds.foregroundCount = sqlite3_column_int(stmt, 2);
            ds.foregroundMinutes = static_cast<float>(sqlite3_column_double(stmt, 3));
            ds.backgroundCount = sqlite3_column_int(stmt, 4);
            ds.backgroundMinutes = static_cast<float>(sqlite3_column_double(stmt, 5));
            result.push_back(std::move(ds));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void LongTermMemory::RecordAppTransition(const std::string& fromApp, const std::string& toApp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || fromApp.empty() || toApp.empty() || fromApp == toApp) return;

    std::string now = GetCurrentTimeString();
    const char* sql =
        "INSERT INTO app_transitions (from_app, to_app, count, last_time) VALUES(?1,?2,1,?3) "
        "ON CONFLICT(from_app, to_app) DO UPDATE SET count = count + 1, last_time = ?3";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fromApp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, toApp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<AppTransition> LongTermMemory::GetAppTransitions(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AppTransition> result;
    if (!db_) return result;

    std::string sql = "SELECT from_app, to_app, count, last_time FROM app_transitions ORDER BY count DESC";
    if (limit > 0) sql += " LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AppTransition tr;
            tr.fromApp = SafeText(sqlite3_column_text(stmt, 0));
            tr.toApp = SafeText(sqlite3_column_text(stmt, 1));
            tr.count = sqlite3_column_int(stmt, 2);
            tr.lastTime = SafeText(sqlite3_column_text(stmt, 3));
            result.push_back(std::move(tr));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void LongTermMemory::RecordWebService(const std::string& serviceName, const std::string& browserProcess, float durationMinutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string now = GetCurrentTimeString();

    auto it = std::find_if(webServices_.begin(), webServices_.end(),
        [&](const WebServiceRecord& r) { return r.serviceName == serviceName; });

    if (it != webServices_.end()) {
        it->totalMinutes += durationMinutes;
        it->visitCount++;
        it->lastVisited = now;
        if (!browserProcess.empty()) it->browserProcess = browserProcess;
    } else {
        WebServiceRecord rec;
        rec.serviceName = serviceName;
        rec.browserProcess = browserProcess;
        rec.totalMinutes = durationMinutes;
        rec.visitCount = 1;
        rec.firstVisited = now;
        rec.lastVisited = now;
        webServices_.push_back(std::move(rec));
    }
}

void LongTermMemory::SetWebServiceTags(const std::string& serviceName, const std::vector<std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(webServices_.begin(), webServices_.end(),
        [&](const WebServiceRecord& r) { return r.serviceName == serviceName; });
    if (it != webServices_.end()) {
        it->tags = tags;
    }
}

WebServiceRecord* LongTermMemory::FindWebService(const std::string& serviceName) {
    auto it = std::find_if(webServices_.begin(), webServices_.end(),
        [&](const WebServiceRecord& r) { return r.serviceName == serviceName; });
    return it != webServices_.end() ? &(*it) : nullptr;
}

void LongTermMemory::RecordAppLaunch(const std::string& launchedApp, const std::string& launchedFrom) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || launchedApp.empty() || launchedFrom.empty()) return;

    std::string now = GetCurrentTimeString();
    const char* sql =
        "INSERT INTO app_launches (app, launched_from, count, last_time) VALUES(?1,?2,1,?3) "
        "ON CONFLICT(app, launched_from) DO UPDATE SET count = count + 1, last_time = ?3";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, launchedApp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, launchedFrom.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<AppLaunchRecord> LongTermMemory::GetAppLaunchRecords(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AppLaunchRecord> result;
    if (!db_) return result;

    std::string sql = "SELECT app, launched_from, count, last_time FROM app_launches ORDER BY count DESC";
    if (limit > 0) sql += " LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            AppLaunchRecord al;
            al.launchedApp = SafeText(sqlite3_column_text(stmt, 0));
            al.launchedFrom = SafeText(sqlite3_column_text(stmt, 1));
            al.count = sqlite3_column_int(stmt, 2);
            al.lastTime = SafeText(sqlite3_column_text(stmt, 3));
            result.push_back(std::move(al));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void LongTermMemory::AddFact(const std::string& category, const std::string& content, float importance) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;

    const char* sql = "INSERT INTO facts (category, content, learned_at, importance) VALUES(?1,?2,?3,?4)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string now = GetCurrentTimeString();
        sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, importance);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<MemoryFact> LongTermMemory::GetFacts(const std::string& category) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MemoryFact> result;
    if (!db_) return result;

    std::string sql = "SELECT category, content, learned_at, importance FROM facts";
    if (!category.empty()) sql += " WHERE category = ?1";
    sql += " ORDER BY importance DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (!category.empty()) {
            sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MemoryFact f;
            f.category = SafeText(sqlite3_column_text(stmt, 0));
            f.content = SafeText(sqlite3_column_text(stmt, 1));
            f.learnedAt = SafeText(sqlite3_column_text(stmt, 2));
            f.importance = static_cast<float>(sqlite3_column_double(stmt, 3));
            result.push_back(std::move(f));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int LongTermMemory::GetFactCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return 0;

    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM facts", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

std::string LongTermMemory::BuildContextString() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream ctx;

    if (!userProfile_.name.empty() || !userProfile_.interests.empty()) {
        ctx << "## ユーザー情報\n";
        if (!userProfile_.name.empty()) {
            ctx << "- 名前: " << userProfile_.name << "\n";
        }
        if (!userProfile_.interests.empty()) {
            ctx << "- 興味: ";
            for (size_t i = 0; i < userProfile_.interests.size(); ++i) {
                if (i > 0) ctx << ", ";
                ctx << userProfile_.interests[i];
            }
            ctx << "\n";
        }
        if (!userProfile_.notes.empty()) {
            ctx << "- メモ: " << userProfile_.notes << "\n";
        }
        ctx << "\n";
    }

    if (!appUsage_.empty()) {
        ctx << "## よく使うアプリ\n";
        std::vector<const AppUsageRecord*> sorted;
        for (auto& r : appUsage_) sorted.push_back(&r);
        std::sort(sorted.begin(), sorted.end(),
            [](const AppUsageRecord* a, const AppUsageRecord* b) {
                return (a->totalForegroundMinutes + a->totalBackgroundMinutes) >
                       (b->totalForegroundMinutes + b->totalBackgroundMinutes);
            });
        int count = 0;
        for (auto* r : sorted) {
            if (count >= 5) break;
            ctx << "- " << r->processName;
            if (!r->tags.empty()) {
                ctx << " [";
                for (size_t i = 0; i < r->tags.size(); ++i) {
                    if (i > 0) ctx << ", ";
                    ctx << r->tags[i];
                }
                ctx << "]";
            }
            int fgMin = static_cast<int>(r->totalForegroundMinutes);
            int bgMin = static_cast<int>(r->totalBackgroundMinutes);
            ctx << " (前面 " << fgMin << "分, 背面 " << bgMin << "分, " << r->launchCount << "回)\n";
            ++count;
        }
        ctx << "\n";
    }

    // App transitions (SQLite)
    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT from_app, to_app, count FROM app_transitions ORDER BY count DESC LIMIT 5",
                -1, &stmt, nullptr) == SQLITE_OK) {
            bool hasData = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!hasData) { ctx << "## アプリ遷移パターン\n"; hasData = true; }
                ctx << "- " << SafeText(sqlite3_column_text(stmt, 0))
                    << " → " << SafeText(sqlite3_column_text(stmt, 1))
                    << " (" << sqlite3_column_int(stmt, 2) << "回)\n";
            }
            if (hasData) ctx << "\n";
            sqlite3_finalize(stmt);
        }
    }

    if (!webServices_.empty()) {
        ctx << "## よく使うWebサービス\n";
        std::vector<const WebServiceRecord*> wsSorted;
        for (auto& ws : webServices_) wsSorted.push_back(&ws);
        std::sort(wsSorted.begin(), wsSorted.end(),
            [](const WebServiceRecord* a, const WebServiceRecord* b) {
                return a->totalMinutes > b->totalMinutes;
            });
        int count = 0;
        for (auto* ws : wsSorted) {
            if (count >= 5) break;
            ctx << "- " << ws->serviceName;
            if (!ws->tags.empty()) {
                ctx << " [";
                for (size_t i = 0; i < ws->tags.size(); ++i) {
                    if (i > 0) ctx << ", ";
                    ctx << ws->tags[i];
                }
                ctx << "]";
            }
            int min = static_cast<int>(ws->totalMinutes);
            ctx << " (" << min << "分, " << ws->visitCount << "回)\n";
            ++count;
        }
        ctx << "\n";
    }

    // App launches (SQLite)
    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT launched_from, app, count FROM app_launches ORDER BY count DESC LIMIT 5",
                -1, &stmt, nullptr) == SQLITE_OK) {
            bool hasData = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!hasData) { ctx << "## アプリ起動パターン\n"; hasData = true; }
                ctx << "- " << SafeText(sqlite3_column_text(stmt, 0))
                    << " から " << SafeText(sqlite3_column_text(stmt, 1))
                    << " を起動 (" << sqlite3_column_int(stmt, 2) << "回)\n";
            }
            if (hasData) ctx << "\n";
            sqlite3_finalize(stmt);
        }
    }

    // Facts (SQLite)
    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT category, content FROM facts ORDER BY importance DESC LIMIT 10",
                -1, &stmt, nullptr) == SQLITE_OK) {
            bool hasData = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!hasData) { ctx << "## 記憶した事実\n"; hasData = true; }
                ctx << "- [" << SafeText(sqlite3_column_text(stmt, 0))
                    << "] " << SafeText(sqlite3_column_text(stmt, 1)) << "\n";
            }
            sqlite3_finalize(stmt);
        }
    }

    return ctx.str();
}

std::vector<MemoryFact> LongTermMemory::SearchFacts(const std::string& query, int maxResults) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || query.empty()) return {};

    const char* sql =
        "SELECT category, content, learned_at, importance FROM facts "
        "WHERE content LIKE ?1 OR category LIKE ?1 "
        "ORDER BY importance DESC LIMIT ?2";

    std::vector<MemoryFact> result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string pattern = "%" + query + "%";
        sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, maxResults);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            MemoryFact f;
            f.category = SafeText(sqlite3_column_text(stmt, 0));
            f.content = SafeText(sqlite3_column_text(stmt, 1));
            f.learnedAt = SafeText(sqlite3_column_text(stmt, 2));
            f.importance = static_cast<float>(sqlite3_column_double(stmt, 3));
            result.push_back(std::move(f));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::string LongTermMemory::BuildContextString(const std::string& query) const {
    std::string base = BuildContextString();

    if (query.empty()) return base;

    auto relevant = SearchFacts(query, 5);
    if (relevant.empty()) return base;

    std::ostringstream ctx;
    ctx << base;
    ctx << "\n## 今の話題に関連する記憶\n";
    for (auto& f : relevant) {
        ctx << "- [" << f.category << "] " << f.content << " (" << f.learnedAt << ")\n";
    }
    return ctx.str();
}

bool LongTermMemory::LoadJson(const std::string& filePath, std::string& outContent) const {
    std::ifstream file(filePath);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    outContent = ss.str();
    return !outContent.empty();
}

bool LongTermMemory::SaveJson(const std::string& filePath, const std::string& content) const {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

std::string LongTermMemory::GetCurrentTimeString() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_s(&tm_buf, &time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
    return buf;
}
