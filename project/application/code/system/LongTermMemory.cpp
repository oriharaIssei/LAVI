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

    ExecSql(
        "CREATE TABLE IF NOT EXISTS parallel_sessions ("
        "  fg_app TEXT NOT NULL,"
        "  bg_app TEXT NOT NULL,"
        "  fg_context TEXT DEFAULT '',"
        "  bg_context TEXT DEFAULT '',"
        "  count INTEGER DEFAULT 0,"
        "  total_minutes REAL DEFAULT 0,"
        "  last_seen TEXT,"
        "  PRIMARY KEY(fg_app, bg_app)"
        ")");

    ExecSql(
        "CREATE TABLE IF NOT EXISTS hourly_activity ("
        "  date TEXT NOT NULL,"
        "  hour INTEGER NOT NULL,"
        "  process_name TEXT NOT NULL,"
        "  context TEXT DEFAULT '',"
        "  fg_minutes REAL DEFAULT 0,"
        "  bg_minutes REAL DEFAULT 0,"
        "  PRIMARY KEY(date, hour, process_name)"
        ")");

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
        userProfile_.dislikedTopics = extractArray(profileJson, "dislikedTopics");
        userProfile_.notes = extractString(profileJson, "notes");

        // interests: {"カテゴリ": [{"k":"...","s":N,"t":"..."}, ...], ...}
        auto extractEntryInt = [](const std::string& obj, const std::string& key) -> int {
            std::string search = "\"" + key + "\":";
            auto p = obj.find(search);
            if (p == std::string::npos) return 0;
            p += search.size();
            return std::atoi(obj.c_str() + p);
        };
        auto interestsPos = profileJson.find("\"interests\":{");
        if (interestsPos != std::string::npos) {
            auto braceStart = profileJson.find('{', interestsPos + 11);
            if (braceStart != std::string::npos) {
                int depth = 1;
                size_t braceEnd = braceStart + 1;
                while (braceEnd < profileJson.size() && depth > 0) {
                    if (profileJson[braceEnd] == '{') ++depth;
                    else if (profileJson[braceEnd] == '}') --depth;
                    ++braceEnd;
                }
                std::string obj = profileJson.substr(braceStart + 1, braceEnd - braceStart - 2);
                size_t cur = 0;
                while (cur < obj.size()) {
                    // カテゴリ名
                    auto k1 = obj.find('"', cur);
                    if (k1 == std::string::npos) break;
                    auto k2 = obj.find('"', k1 + 1);
                    if (k2 == std::string::npos) break;
                    std::string cat = obj.substr(k1 + 1, k2 - k1 - 1);
                    auto arrStart = obj.find('[', k2);
                    if (arrStart == std::string::npos) break;
                    auto arrEnd = obj.find(']', arrStart);
                    if (arrEnd == std::string::npos) break;
                    std::string arrStr = obj.substr(arrStart + 1, arrEnd - arrStart - 1);

                    // エントリが {"k":"...","s":N,"t":"..."} 形式かどうか
                    if (arrStr.find("\"k\"") != std::string::npos) {
                        size_t ec = 0;
                        while (ec < arrStr.size()) {
                            auto eStart = arrStr.find('{', ec);
                            if (eStart == std::string::npos) break;
                            auto eEnd = arrStr.find('}', eStart);
                            if (eEnd == std::string::npos) break;
                            std::string eObj = arrStr.substr(eStart, eEnd - eStart + 1);

                            InterestEntry entry;
                            entry.keyword = extractString(eObj, "k");
                            entry.score = extractEntryInt(eObj, "s");
                            entry.lastSeen = extractString(eObj, "t");
                            if (entry.score <= 0) entry.score = 1;
                            if (!entry.keyword.empty()) {
                                userProfile_.interests[cat].push_back(std::move(entry));
                            }
                            ec = eEnd + 1;
                        }
                    } else {
                        // 簡易形式フォールバック: ["str1","str2"]
                        size_t ac = 0;
                        while (ac < arrStr.size()) {
                            auto q1 = arrStr.find('"', ac);
                            if (q1 == std::string::npos) break;
                            auto q2 = arrStr.find('"', q1 + 1);
                            if (q2 == std::string::npos) break;
                            InterestEntry entry;
                            entry.keyword = arrStr.substr(q1 + 1, q2 - q1 - 1);
                            if (!entry.keyword.empty()) {
                                userProfile_.interests[cat].push_back(std::move(entry));
                            }
                            ac = q2 + 1;
                        }
                    }
                    cur = arrEnd + 1;
                }
            }
        } else {
            // 旧形式フォールバック: "interests":["a","b"] → "その他" に格納
            auto old = extractArray(profileJson, "interests");
            for (auto& s : old) {
                InterestEntry entry;
                entry.keyword = std::move(s);
                userProfile_.interests["\xe3\x81\x9d\xe3\x81\xae\xe4\xbb\x96"].push_back(std::move(entry));
            }
        }
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
        json << "  \"interests\":{\n";
        {
            size_t catIdx = 0;
            for (auto& [cat, items] : userProfile_.interests) {
                if (catIdx > 0) json << ",\n";
                json << "    \"" << cat << "\":[\n";
                for (size_t i = 0; i < items.size(); ++i) {
                    if (i > 0) json << ",\n";
                    json << "      {\"k\":\"" << items[i].keyword
                         << "\",\"s\":" << items[i].score
                         << ",\"t\":\"" << items[i].lastSeen << "\"}";
                }
                json << "\n    ]";
                ++catIdx;
            }
        }
        json << "\n  },\n";
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

void LongTermMemory::ImportWebServiceFromHistory(const std::string& serviceName, const std::string& browserProcess, int visitCount) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(webServices_.begin(), webServices_.end(),
        [&](const WebServiceRecord& r) { return r.serviceName == serviceName; });

    if (it != webServices_.end()) {
        if (it->browserProcess.empty() && !browserProcess.empty()) {
            it->browserProcess = browserProcess;
        }
    } else {
        WebServiceRecord rec;
        rec.serviceName = serviceName;
        rec.browserProcess = browserProcess;
        rec.totalMinutes = 0.0f;
        rec.visitCount = visitCount;
        rec.firstVisited = GetCurrentTimeString();
        rec.lastVisited = rec.firstVisited;
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

void LongTermMemory::RecordParallelUsage(const std::string& foregroundApp, const std::string& backgroundApp,
                                          const std::string& fgContext, const std::string& bgContext, float minutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || foregroundApp.empty() || backgroundApp.empty()) return;

    std::string now = GetCurrentTimeString();
    const char* sql =
        "INSERT INTO parallel_sessions (fg_app, bg_app, fg_context, bg_context, count, total_minutes, last_seen) "
        "VALUES(?1,?2,?3,?4,1,?5,?6) "
        "ON CONFLICT(fg_app, bg_app) DO UPDATE SET "
        "count = count + 1, total_minutes = total_minutes + ?5, last_seen = ?6, "
        "fg_context = ?3, bg_context = ?4";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, foregroundApp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, backgroundApp.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, fgContext.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, bgContext.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, minutes);
        sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<ParallelHabit> LongTermMemory::GetParallelHabits(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ParallelHabit> result;
    if (!db_) return result;

    std::string sql =
        "SELECT fg_app, bg_app, fg_context, bg_context, count, total_minutes, last_seen "
        "FROM parallel_sessions ORDER BY count DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ParallelHabit h;
            h.foregroundApp = SafeText(sqlite3_column_text(stmt, 0));
            h.backgroundApp = SafeText(sqlite3_column_text(stmt, 1));
            h.foregroundContext = SafeText(sqlite3_column_text(stmt, 2));
            h.backgroundContext = SafeText(sqlite3_column_text(stmt, 3));
            h.count = sqlite3_column_int(stmt, 4);
            h.totalMinutes = static_cast<float>(sqlite3_column_double(stmt, 5));
            h.lastSeen = SafeText(sqlite3_column_text(stmt, 6));
            result.push_back(std::move(h));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void LongTermMemory::RecordHourlyActivity(const std::string& processName, const std::string& context,
                                           int hour, float fgMinutes, float bgMinutes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || processName.empty()) return;

    std::string today = GetCurrentTimeString().substr(0, 10);
    const char* sql =
        "INSERT INTO hourly_activity (date, hour, process_name, context, fg_minutes, bg_minutes) "
        "VALUES(?1,?2,?3,?4,?5,?6) "
        "ON CONFLICT(date, hour, process_name) DO UPDATE SET "
        "fg_minutes = fg_minutes + ?5, bg_minutes = bg_minutes + ?6, context = ?4";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, today.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, hour);
        sqlite3_bind_text(stmt, 3, processName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, context.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, fgMinutes);
        sqlite3_bind_double(stmt, 6, bgMinutes);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<HourlyPattern> LongTermMemory::GetHourlyPatterns() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<HourlyPattern> result;
    if (!db_) return result;

    const char* sql =
        "SELECT hour, process_name, context, "
        "SUM(fg_minutes) as total_fg, COUNT(DISTINCT date) as days "
        "FROM hourly_activity "
        "GROUP BY hour, process_name "
        "ORDER BY hour ASC, total_fg DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    int lastHour = -1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int hour = sqlite3_column_int(stmt, 0);
        if (hour == lastHour) continue;
        lastHour = hour;

        HourlyPattern p;
        p.hour = hour;
        p.topApp = SafeText(sqlite3_column_text(stmt, 1));
        p.topContext = SafeText(sqlite3_column_text(stmt, 2));
        p.avgFgMinutes = static_cast<float>(sqlite3_column_double(stmt, 3));
        p.dayCount = sqlite3_column_int(stmt, 4);
        if (p.dayCount > 0) p.avgFgMinutes /= p.dayCount;
        result.push_back(std::move(p));
    }
    sqlite3_finalize(stmt);
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

    // 生活文脈: 現在の時刻と蓄積パターンから「今の状況」を推測
    if (db_) {
        auto sysNow = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(sysNow);
        struct tm tm_buf;
        localtime_s(&tm_buf, &time);
        int currentHour = tm_buf.tm_hour;

        static const char* kDayNames[] = {
            "\xe6\x97\xa5", "\xe6\x9c\x88", "\xe7\x81\xab", "\xe6\xb0\xb4",
            "\xe6\x9c\xa8", "\xe9\x87\x91", "\xe5\x9c\x9f" // 日月火水木金土
        };
        ctx << "## \xe4\xbb\x8a\xe3\x81\xae\xe7\x8a\xb6\xe6\xb3\x81\n"; // 今の状況
        ctx << "- " << kDayNames[tm_buf.tm_wday] << "\xe6\x9b\x9c "
            << currentHour << ":" << (tm_buf.tm_min < 10 ? "0" : "") << tm_buf.tm_min << "\n";

        // この時間帯の典型的なアクティビティ
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT context, SUM(fg_minutes) as total_fg, COUNT(DISTINCT date) as days "
            "FROM hourly_activity WHERE hour = ?1 AND context != '' "
            "GROUP BY context HAVING days >= 2 "
            "ORDER BY total_fg DESC LIMIT 3";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, currentHour);
            bool hasTypical = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!hasTypical) {
                    // この時間帯はいつも:
                    ctx << "- \xe3\x81\x93\xe3\x81\xae\xe6\x99\x82\xe9\x96\x93\xe5\xb8\xaf\xe3\x81\xaf\xe3\x81\x84\xe3\x81\xa4\xe3\x82\x82: ";
                    hasTypical = true;
                } else {
                    ctx << ", ";
                }
                ctx << SafeText(sqlite3_column_text(stmt, 0));
            }
            if (hasTypical) ctx << "\n";
            sqlite3_finalize(stmt);
        }

        // この時間帯のよくあるながら習慣
        const char* parallelSql =
            "SELECT p.fg_context, p.bg_context, p.count "
            "FROM parallel_sessions p "
            "INNER JOIN hourly_activity h ON h.process_name IN (p.fg_app, p.bg_app) "
            "WHERE h.hour = ?1 AND p.count >= 3 "
            "GROUP BY p.fg_context, p.bg_context "
            "ORDER BY p.count DESC LIMIT 2";
        if (sqlite3_prepare_v2(db_, parallelSql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, currentHour);
            bool hasParallel = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string fgCtx = SafeText(sqlite3_column_text(stmt, 0));
                std::string bgCtx = SafeText(sqlite3_column_text(stmt, 1));
                if (fgCtx.empty() && bgCtx.empty()) continue;
                if (!hasParallel) {
                    // よくある組み合わせ:
                    ctx << "- \xe3\x82\x88\xe3\x81\x8f\xe3\x81\x82\xe3\x82\x8b\xe7\xb5\x84\xe3\x81\xbf\xe5\x90\x88\xe3\x82\x8f\xe3\x81\x9b: ";
                    hasParallel = true;
                } else {
                    ctx << ", ";
                }
                if (!fgCtx.empty() && !bgCtx.empty()) {
                    ctx << fgCtx << "+" << bgCtx;
                } else {
                    ctx << (fgCtx.empty() ? bgCtx : fgCtx);
                }
            }
            if (hasParallel) ctx << "\n";
            sqlite3_finalize(stmt);
        }

        ctx << "\n";
    }

    if (!userProfile_.name.empty() || !userProfile_.interests.empty()) {
        ctx << "## \xe3\x83\xa6\xe3\x83\xbc\xe3\x82\xb6\xe3\x83\xbc\xe6\x83\x85\xe5\xa0\xb1\n";
        if (!userProfile_.name.empty()) {
            ctx << "- \xe5\x90\x8d\xe5\x89\x8d: " << userProfile_.name << "\n";
        }
        for (auto& [cat, items] : userProfile_.interests) {
            if (items.empty()) continue;
            // score上位10件を表示
            auto sorted = items;
            std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) {
                return a.score > b.score;
            });
            ctx << "- " << cat << ": ";
            for (size_t i = 0; i < sorted.size() && i < 10; ++i) {
                if (i > 0) ctx << ", ";
                ctx << sorted[i].keyword;
            }
            ctx << "\n";
        }
        if (!userProfile_.notes.empty()) {
            ctx << "- \xe3\x83\xa1\xe3\x83\xa2: " << userProfile_.notes << "\n";
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
                return (a->totalMinutes + a->visitCount) > (b->totalMinutes + b->visitCount);
            });
        int count = 0;
        for (auto* ws : wsSorted) {
            if (count >= 10) break;
            ctx << "- " << ws->serviceName;
            if (!ws->browserProcess.empty()) {
                ctx << " (" << ws->browserProcess << ")";
            }
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

    // Parallel habits (SQLite)
    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT fg_app, bg_app, fg_context, bg_context, count, total_minutes "
                "FROM parallel_sessions WHERE count >= 3 ORDER BY count DESC LIMIT 8",
                -1, &stmt, nullptr) == SQLITE_OK) {
            bool hasData = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!hasData) { ctx << "\n## ながら習慣\n"; hasData = true; }
                std::string fg = SafeText(sqlite3_column_text(stmt, 0));
                std::string bg = SafeText(sqlite3_column_text(stmt, 1));
                std::string fgCtx = SafeText(sqlite3_column_text(stmt, 2));
                std::string bgCtx = SafeText(sqlite3_column_text(stmt, 3));
                int cnt = sqlite3_column_int(stmt, 4);
                int min = static_cast<int>(sqlite3_column_double(stmt, 5));
                ctx << "- " << fg;
                if (!fgCtx.empty()) ctx << "(" << fgCtx << ")";
                ctx << " + " << bg;
                if (!bgCtx.empty()) ctx << "(" << bgCtx << ")";
                ctx << " [" << cnt << "回, " << min << "分]\n";
            }
            if (hasData) ctx << "\n";
            sqlite3_finalize(stmt);
        }
    }

    // Hourly activity patterns (SQLite)
    if (db_) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT hour, context, SUM(fg_minutes) as total_fg, COUNT(DISTINCT date) as days "
            "FROM hourly_activity WHERE context != '' "
            "GROUP BY hour, context "
            "HAVING days >= 2 "
            "ORDER BY hour ASC, total_fg DESC";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            bool hasData = false;
            int lastHour = -1;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int hour = sqlite3_column_int(stmt, 0);
                if (hour == lastHour) continue;
                lastHour = hour;
                if (!hasData) { ctx << "\n## 時間帯別パターン\n"; hasData = true; }
                std::string context = SafeText(sqlite3_column_text(stmt, 1));
                float totalFg = static_cast<float>(sqlite3_column_double(stmt, 2));
                int days = sqlite3_column_int(stmt, 3);
                float avg = (days > 0) ? totalFg / days : 0.0f;
                ctx << "- " << hour << ":00〜" << hour + 1 << ":00: "
                    << context << " (平均" << static_cast<int>(avg) << "分/日, "
                    << days << "日観測)\n";
            }
            if (hasData) ctx << "\n";
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

std::string LongTermMemory::BuildCompactContext(const std::string& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ctx;

    if (!userProfile_.interests.empty()) {
        ctx << "## \xe3\x83\xa6\xe3\x83\xbc\xe3\x82\xb6\xe3\x83\xbc\xe3\x81\xae\xe8\x88\x88\xe5\x91\xb3\n";
        for (auto& [cat, items] : userProfile_.interests) {
            if (items.empty()) continue;
            auto sorted = items;
            std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) {
                return a.score > b.score;
            });
            ctx << cat << ": ";
            for (size_t i = 0; i < sorted.size() && i < 5; ++i) {
                if (i > 0) ctx << ", ";
                ctx << sorted[i].keyword;
            }
            ctx << "\n";
        }
        ctx << "\n";
    }

    if (!webServices_.empty()) {
        ctx << "## Webサービス利用\n";
        std::vector<const WebServiceRecord*> wsSorted;
        for (auto& ws : webServices_) wsSorted.push_back(&ws);
        std::sort(wsSorted.begin(), wsSorted.end(),
            [](const WebServiceRecord* a, const WebServiceRecord* b) {
                return (a->totalMinutes + a->visitCount) > (b->totalMinutes + b->visitCount);
            });
        int count = 0;
        for (auto* ws : wsSorted) {
            if (count >= 5) break;
            ctx << "- " << ws->serviceName;
            if (!ws->browserProcess.empty()) ctx << "(" << ws->browserProcess << ")";
            ctx << " " << ws->visitCount << "回\n";
            ++count;
        }
        ctx << "\n";
    }

    if (db_ && !query.empty()) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT content FROM facts "
            "WHERE content LIKE ?1 OR category LIKE ?1 "
            "ORDER BY importance DESC LIMIT 3";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            std::string pattern = "%" + query + "%";
            sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
            bool hasData = false;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                if (!hasData) { ctx << "## 関連する記憶\n"; hasData = true; }
                ctx << "- " << SafeText(sqlite3_column_text(stmt, 0)) << "\n";
            }
            sqlite3_finalize(stmt);
        }
    }

    return ctx.str();
}

void LongTermMemory::AddInterest(const std::string& category, const std::string& keyword) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& items = userProfile_.interests[category];

    // 既存なら score を強化
    for (auto& e : items) {
        if (e.keyword == keyword) {
            ++e.score;
            // 現在日時を更新
            auto now = std::chrono::system_clock::now();
            auto tt = std::chrono::system_clock::to_time_t(now);
            char buf[20];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&tt));
            e.lastSeen = buf;
            return;
        }
    }

    // 新規追加
    InterestEntry entry;
    entry.keyword = keyword;
    entry.score = 1;
    {
        auto now = std::chrono::system_clock::now();
        auto tt = std::chrono::system_clock::to_time_t(now);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", std::localtime(&tt));
        entry.lastSeen = buf;
    }
    items.push_back(std::move(entry));

    // 上限超過 → 最低スコアを淘汰
    if (static_cast<int>(items.size()) > UserProfile::kMaxEntriesPerCategory) {
        std::sort(items.begin(), items.end(), [](auto& a, auto& b) {
            return a.score < b.score;
        });
        items.erase(items.begin());
    }
}

std::vector<std::string> LongTermMemory::GetInterests(const std::string& category, int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = userProfile_.interests.find(category);
    if (it == userProfile_.interests.end()) return {};

    // score 降順でソートしたコピー
    auto sorted = it->second;
    std::sort(sorted.begin(), sorted.end(), [](auto& a, auto& b) {
        return a.score > b.score;
    });

    std::vector<std::string> result;
    for (int i = 0; i < limit && i < static_cast<int>(sorted.size()); ++i) {
        result.push_back(sorted[i].keyword);
    }
    return result;
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
