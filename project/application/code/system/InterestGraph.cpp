#include "InterestGraph.h"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace {

const char* SafeStr(const unsigned char* p) {
    return p ? reinterpret_cast<const char*>(p) : "";
}

}  // namespace

InterestGraph::InterestGraph() = default;

InterestGraph::~InterestGraph() = default;

void InterestGraph::Initialize(sqlite3* db, const std::string& taxonomyPath) {
    db_ = db;
    if (!db_) return;
    InitTables();
    LoadTaxonomy(taxonomyPath);

    std::lock_guard<std::mutex> lock(mutex_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT alias, entity_id FROM entity_aliases",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            aliasCache_[SafeStr(sqlite3_column_text(stmt, 0))] =
                SafeStr(sqlite3_column_text(stmt, 1));
        }
        sqlite3_finalize(stmt);
    }
}

void InterestGraph::InitTables() {
    auto exec = [this](const char* sql) {
        char* err = nullptr;
        sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
    };

    exec("CREATE TABLE IF NOT EXISTS entities ("
         "  id TEXT PRIMARY KEY,"
         "  canonical_name TEXT NOT NULL,"
         "  entity_type TEXT NOT NULL,"
         "  domain_weights TEXT NOT NULL DEFAULT '{}',"
         "  source TEXT NOT NULL DEFAULT '',"
         "  provisional INTEGER NOT NULL DEFAULT 1,"
         "  created_at TEXT NOT NULL,"
         "  updated_at TEXT NOT NULL"
         ")");
    exec("ALTER TABLE entities ADD COLUMN provisional INTEGER NOT NULL DEFAULT 1");

    exec("CREATE TABLE IF NOT EXISTS entity_aliases ("
         "  alias TEXT PRIMARY KEY,"
         "  entity_id TEXT NOT NULL,"
         "  FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE"
         ")");
    exec("CREATE INDEX IF NOT EXISTS idx_aliases_entity ON entity_aliases(entity_id)");

    exec("CREATE TABLE IF NOT EXISTS relations ("
         "  from_id TEXT NOT NULL,"
         "  to_id TEXT NOT NULL,"
         "  rel_type TEXT NOT NULL,"
         "  weight REAL NOT NULL DEFAULT 1.0,"
         "  source TEXT NOT NULL DEFAULT '',"
         "  created_at TEXT NOT NULL,"
         "  PRIMARY KEY(from_id, to_id, rel_type)"
         ")");

    exec("CREATE TABLE IF NOT EXISTS sessions ("
         "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  start_time TEXT NOT NULL,"
         "  end_time TEXT,"
         "  duration_sec INTEGER DEFAULT 0,"
         "  event_count INTEGER DEFAULT 0,"
         "  dominant_entity TEXT"
         ")");
    exec("ALTER TABLE sessions ADD COLUMN event_count INTEGER DEFAULT 0");
    exec("ALTER TABLE sessions ADD COLUMN dominant_entity TEXT");
    exec("CREATE INDEX IF NOT EXISTS idx_sessions_time ON sessions(start_time)");

    exec("CREATE TABLE IF NOT EXISTS interaction_events ("
         "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
         "  entity_id TEXT NOT NULL,"
         "  session_id INTEGER,"
         "  timestamp TEXT NOT NULL,"
         "  event_type TEXT NOT NULL,"
         "  duration_sec INTEGER NOT NULL DEFAULT 0,"
         "  depth TEXT NOT NULL DEFAULT 'browse',"
         "  source TEXT NOT NULL DEFAULT '',"
         "  processed INTEGER NOT NULL DEFAULT 0"
         ")");
    exec("ALTER TABLE interaction_events ADD COLUMN processed INTEGER NOT NULL DEFAULT 0");
    exec("CREATE INDEX IF NOT EXISTS idx_events_entity ON interaction_events(entity_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_events_time ON interaction_events(timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_events_session ON interaction_events(session_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_events_type ON interaction_events(event_type)");
    exec("CREATE INDEX IF NOT EXISTS idx_events_processed ON interaction_events(processed)");

    exec("CREATE TABLE IF NOT EXISTS entity_profiles ("
         "  entity_id TEXT PRIMARY KEY,"
         "  attraction_vector TEXT NOT NULL DEFAULT '{}',"
         "  computed_at TEXT NOT NULL DEFAULT '',"
         "  event_horizon_days INTEGER NOT NULL DEFAULT 30,"
         "  FOREIGN KEY(entity_id) REFERENCES entities(id) ON DELETE CASCADE"
         ")");
}

void InterestGraph::LoadTaxonomy(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    auto extractStringArray = [&](const std::string& key) -> std::vector<std::string> {
        std::vector<std::string> result;
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return result;
        auto arrStart = json.find('[', pos);
        if (arrStart == std::string::npos) return result;
        auto arrEnd = json.find(']', arrStart);
        if (arrEnd == std::string::npos) return result;
        size_t cur = arrStart + 1;
        while (cur < arrEnd) {
            auto q1 = json.find('"', cur);
            if (q1 == std::string::npos || q1 >= arrEnd) break;
            auto q2 = json.find('"', q1 + 1);
            if (q2 == std::string::npos || q2 >= arrEnd) break;
            result.push_back(json.substr(q1 + 1, q2 - q1 - 1));
            cur = q2 + 1;
        }
        return result;
    };

    // entity_types: {"person": ["artist", ...], ...}
    auto etPos = json.find("\"entity_types\"");
    if (etPos != std::string::npos) {
        auto braceStart = json.find('{', etPos + 14);
        if (braceStart != std::string::npos) {
            int depth = 1;
            size_t braceEnd = braceStart + 1;
            while (braceEnd < json.size() && depth > 0) {
                if (json[braceEnd] == '{') ++depth;
                if (json[braceEnd] == '}') --depth;
                ++braceEnd;
            }
            std::string block = json.substr(braceStart, braceEnd - braceStart);
            size_t pos = 0;
            while (pos < block.size()) {
                auto q1 = block.find('"', pos);
                if (q1 == std::string::npos) break;
                auto q2 = block.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                std::string parent = block.substr(q1 + 1, q2 - q1 - 1);

                auto arrS = block.find('[', q2);
                if (arrS == std::string::npos) break;
                auto arrE = block.find(']', arrS);
                if (arrE == std::string::npos) break;

                validEntityTypes_.insert(parent);
                size_t cur = arrS + 1;
                while (cur < arrE) {
                    auto s1 = block.find('"', cur);
                    if (s1 == std::string::npos || s1 >= arrE) break;
                    auto s2 = block.find('"', s1 + 1);
                    if (s2 == std::string::npos || s2 >= arrE) break;
                    std::string child = block.substr(s1 + 1, s2 - s1 - 1);
                    validEntityTypes_.insert(parent + "/" + child);
                    cur = s2 + 1;
                }
                pos = arrE + 1;
            }
        }
    }

    for (auto& s : extractStringArray("event_types")) validEventTypes_.insert(s);
    for (auto& s : extractStringArray("relation_types")) validRelationTypes_.insert(s);
    for (auto& s : extractStringArray("depth_levels")) validDepths_.insert(s);

    // session_idle_timeout_sec
    auto timeoutPos = json.find("\"session_idle_timeout_sec\"");
    if (timeoutPos != std::string::npos) {
        auto colonPos = json.find(':', timeoutPos);
        if (colonPos != std::string::npos) {
            sessionIdleTimeoutSec_ = std::atoi(json.c_str() + colonPos + 1);
            if (sessionIdleTimeoutSec_ <= 0) sessionIdleTimeoutSec_ = 900;
        }
    }
}

std::string InterestGraph::Normalize(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (unsigned char c : input) {
        if (c == ' ' || c == '\t') {
            if (!result.empty() && result.back() != ' ') result += ' ';
        } else if (c >= 'A' && c <= 'Z') {
            result += static_cast<char>(c + 32);
        } else {
            result += static_cast<char>(c);
        }
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    while (!result.empty() && result.front() == ' ') result.erase(0, 1);
    return result;
}

std::string InterestGraph::Slugify(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    bool hasAsciiContent = false;
    for (unsigned char c : input) {
        if (c >= 'A' && c <= 'Z') {
            result += static_cast<char>(c + 32);
            hasAsciiContent = true;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            result += static_cast<char>(c);
            hasAsciiContent = true;
        } else if (c == ' ' || c == '-' || c == '.' || c == '+') {
            if (!result.empty() && result.back() != '_') result += '_';
        }
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    if (!hasAsciiContent || result.empty()) {
        result = "ent_" + HashId(Normalize(input));
    }

    // Handle collision
    std::string candidate = result;
    int suffix = 2;
    while (true) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT 1 FROM entities WHERE id = ?1",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, candidate.c_str(), -1, SQLITE_TRANSIENT);
            bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
            sqlite3_finalize(stmt);
            if (!exists) break;
        } else {
            break;
        }
        candidate = result + "_" + std::to_string(suffix++);
    }
    return candidate;
}

std::string InterestGraph::InferEntityType(const std::string& sourceHint) const {
    if (sourceHint == "app_usage_game") return "work/game";
    if (sourceHint == "app_usage") return "system/tool";
    if (sourceHint == "browser_youtube") return "work/video";
    if (sourceHint == "browser_music") return "person/artist";
    if (sourceHint == "browser") return "work/article";
    if (sourceHint == "browser_service") return "system/platform";
    if (sourceHint == "browser_history_music") return "person/artist";
    if (sourceHint == "browser_history_game") return "work/game";
    if (sourceHint == "browser_history_anime") return "work/anime";
    if (sourceHint == "browser_history_manga") return "work/manga";
    if (sourceHint == "browser_history_video") return "work/video";
    if (sourceHint == "browser_history_programming") return "concept/framework";
    if (sourceHint == "browser_history_tech") return "concept/framework";
    if (sourceHint == "browser_history_sports") return "action/sport";
    if (sourceHint == "browser_history") return "concept/theory";
    if (sourceHint == "conversation") return "concept/theory";
    if (sourceHint == "conversation_music") return "person/artist";
    if (sourceHint == "conversation_game") return "work/game";
    if (sourceHint == "conversation_anime") return "work/anime";
    if (sourceHint == "conversation_video") return "work/video";
    if (sourceHint == "conversation_programming") return "concept/framework";
    if (sourceHint == "conversation_tech") return "concept/framework";
    if (sourceHint == "conversation_person") return "person/artist";
    if (sourceHint == "conversation_service") return "system/platform";
    return "concept/theory";
}

std::string InterestGraph::InferDomainWeights(const std::string& sourceHint) const {
    if (sourceHint == "app_usage_game") return "{\"gaming\":1.0}";
    if (sourceHint == "app_usage") return "{\"engineering/software\":0.5}";
    if (sourceHint == "browser_youtube") return "{\"visual_media/youtube\":1.0}";
    if (sourceHint == "browser_music") return "{\"music\":1.0}";
    if (sourceHint == "browser_history_music") return "{\"music\":1.0}";
    if (sourceHint == "browser_history_game") return "{\"gaming\":1.0}";
    if (sourceHint == "browser_history_anime") return "{\"visual_media/anime\":1.0}";
    if (sourceHint == "browser_history_manga") return "{\"narrative/manga\":1.0}";
    if (sourceHint == "browser_history_video") return "{\"visual_media/youtube\":1.0}";
    if (sourceHint == "browser_history_programming") return "{\"engineering/software\":1.0}";
    if (sourceHint == "browser_history_tech") return "{\"engineering/software\":0.8}";
    if (sourceHint == "browser_history_sports") return "{\"lifestyle/fitness\":0.8}";
    if (sourceHint == "conversation_music") return "{\"music\":1.0}";
    if (sourceHint == "conversation_game") return "{\"gaming\":1.0}";
    if (sourceHint == "conversation_anime") return "{\"visual_media/anime\":1.0}";
    if (sourceHint == "conversation_video") return "{\"visual_media/youtube\":1.0}";
    if (sourceHint == "conversation_programming") return "{\"engineering/software\":1.0}";
    if (sourceHint == "conversation_tech") return "{\"engineering/software\":0.8}";
    return "{}";
}

std::string InterestGraph::HashId(const std::string& input) {
    unsigned long hash = 5381;
    for (unsigned char c : input) {
        hash = ((hash << 5) + hash) + c;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%08lx", hash & 0xFFFFFFFF);
    return buf;
}

long long InterestGraph::ParseEpochSec(const std::string& iso) {
    if (iso.size() < 19) return 0;
    struct tm t = {};
    t.tm_year = std::atoi(iso.c_str()) - 1900;
    t.tm_mon = std::atoi(iso.c_str() + 5) - 1;
    t.tm_mday = std::atoi(iso.c_str() + 8);
    t.tm_hour = std::atoi(iso.c_str() + 11);
    t.tm_min = std::atoi(iso.c_str() + 14);
    t.tm_sec = std::atoi(iso.c_str() + 17);
    t.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&t));
}

std::string InterestGraph::MergeDomainWeightsJson(const std::string& existing, const std::string& incoming) {
    auto parse = [](const std::string& json) {
        std::vector<std::pair<std::string, double>> result;
        size_t pos = 0;
        while (pos < json.size()) {
            auto q1 = json.find('"', pos);
            if (q1 == std::string::npos) break;
            auto q2 = json.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string key = json.substr(q1 + 1, q2 - q1 - 1);
            auto colon = json.find(':', q2);
            if (colon == std::string::npos) break;
            double val = std::atof(json.c_str() + colon + 1);
            result.push_back({key, val});
            auto next = json.find(',', colon);
            pos = (next != std::string::npos) ? next + 1 : json.size();
        }
        return result;
    };

    auto existingPairs = parse(existing);
    auto incomingPairs = parse(incoming);

    std::unordered_map<std::string, double> merged;
    for (auto& [k, v] : existingPairs) merged[k] = v;
    for (auto& [k, v] : incomingPairs) {
        auto it = merged.find(k);
        if (it == merged.end() || v > it->second) merged[k] = v;
    }

    if (merged.empty()) return "{}";

    std::string result = "{";
    bool first = true;
    for (auto& [k, v] : merged) {
        if (!first) result += ",";
        first = false;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        result += "\"" + k + "\":" + buf;
    }
    result += "}";
    return result;
}

void InterestGraph::UpdateDomainWeights(const std::string& entityId, const std::string& sourceHint) {
    if (!db_ || entityId.empty()) return;

    std::string incoming = InferDomainWeights(sourceHint);
    if (incoming == "{}") return;

    std::string existing = "{}";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT domain_weights FROM entities WHERE id = ?1",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, entityId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            existing = SafeStr(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    std::string merged = MergeDomainWeightsJson(existing, incoming);
    if (merged == existing) return;

    std::string now = GetCurrentTimeISO();
    if (sqlite3_prepare_v2(db_,
            "UPDATE entities SET domain_weights = ?1, updated_at = ?2 WHERE id = ?3",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, merged.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, entityId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::string InterestGraph::GetCurrentTimeISO() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return buf;
}

std::string InterestGraph::ResolveEntity(const std::string& inputText, const std::string& sourceHint) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || inputText.empty()) return "";

    std::string normalized = Normalize(inputText);
    if (normalized.empty()) return "";

    // Step 1: alias cache
    auto cacheIt = aliasCache_.find(normalized);
    if (cacheIt != aliasCache_.end()) return cacheIt->second;

    // Step 2: alias table
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT entity_id FROM entity_aliases WHERE alias = ?1",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string entityId = SafeStr(sqlite3_column_text(stmt, 0));
                sqlite3_finalize(stmt);
                aliasCache_[normalized] = entityId;
                UpdateDomainWeights(entityId, sourceHint);
                return entityId;
            }
            sqlite3_finalize(stmt);
        }
    }

    // Step 3: canonical_name match
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT id FROM entities WHERE lower(canonical_name) = ?1",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string entityId = SafeStr(sqlite3_column_text(stmt, 0));
                sqlite3_finalize(stmt);

                // Register as alias for future lookups
                sqlite3_stmt* ins = nullptr;
                if (sqlite3_prepare_v2(db_,
                        "INSERT OR IGNORE INTO entity_aliases (alias, entity_id) VALUES(?1,?2)",
                        -1, &ins, nullptr) == SQLITE_OK) {
                    sqlite3_bind_text(ins, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(ins, 2, entityId.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_step(ins);
                    sqlite3_finalize(ins);
                }
                aliasCache_[normalized] = entityId;
                UpdateDomainWeights(entityId, sourceHint);
                return entityId;
            }
            sqlite3_finalize(stmt);
        }
    }

    // Step 4: create new entity
    std::string newId = Slugify(inputText);
    std::string entityType = InferEntityType(sourceHint);
    std::string domainWeights = InferDomainWeights(sourceHint);
    std::string now = GetCurrentTimeISO();

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT INTO entities (id, canonical_name, entity_type, domain_weights, source, created_at, updated_at) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, newId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, inputText.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, entityType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, domainWeights.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, sourceHint.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Register alias
    if (sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO entity_aliases (alias, entity_id) VALUES(?1,?2)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, newId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    aliasCache_[normalized] = newId;
    return newId;
}

std::string InterestGraph::CreateEntity(const std::string& id, const std::string& canonicalName,
                                         const std::string& entityType, const std::string& domainWeightsJson,
                                         const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || id.empty()) return "";

    std::string now = GetCurrentTimeISO();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO entities (id, canonical_name, entity_type, domain_weights, source, provisional, created_at, updated_at) "
            "VALUES(?1,?2,?3,?4,?5,0,?6,?7)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, canonicalName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, entityType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, domainWeightsJson.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::string normalized = Normalize(canonicalName);
    if (!normalized.empty()) {
        if (sqlite3_prepare_v2(db_,
                "INSERT OR IGNORE INTO entity_aliases (alias, entity_id) VALUES(?1,?2)",
                -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        aliasCache_[normalized] = id;
    }

    return id;
}

void InterestGraph::AddAlias(const std::string& alias, const std::string& entityId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || alias.empty() || entityId.empty()) return;

    std::string normalized = Normalize(alias);
    if (normalized.empty()) return;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT OR IGNORE INTO entity_aliases (alias, entity_id) VALUES(?1,?2)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, entityId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    aliasCache_[normalized] = entityId;
}

void InterestGraph::AddRelation(const std::string& fromId, const std::string& toId,
                                 const std::string& relType, float weight, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || fromId.empty() || toId.empty()) return;

    std::string now = GetCurrentTimeISO();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT OR REPLACE INTO relations (from_id, to_id, rel_type, weight, source, created_at) "
            "VALUES(?1,?2,?3,?4,?5,?6)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, fromId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, toId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, relType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 4, weight);
        sqlite3_bind_text(stmt, 5, source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

int InterestGraph::StartSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return -1;

    std::string now = GetCurrentTimeISO();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT INTO sessions (start_time) VALUES(?1)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    activeSessionId_ = static_cast<int>(sqlite3_last_insert_rowid(db_));
    lastEventTime_ = now;
    return activeSessionId_;
}

void InterestGraph::EndSession(int sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || sessionId < 0) return;

    std::string now = GetCurrentTimeISO();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "UPDATE sessions SET end_time = ?1, "
            "duration_sec = (julianday(?1) - julianday(start_time)) * 86400 "
            "WHERE id = ?2 AND end_time IS NULL",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, sessionId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    ComputeSessionSummary(sessionId);

    if (sessionId == activeSessionId_) {
        activeSessionId_ = -1;
    }
}

void InterestGraph::RecordEvent(const std::string& entityId, const std::string& eventType,
                                 int durationSec, const std::string& depth, const std::string& source) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || entityId.empty()) return;

    std::string now = GetCurrentTimeISO();

    // Auto session management
    if (activeSessionId_ >= 0 && !lastEventTime_.empty()) {
        long long diffSec = ParseEpochSec(now) - ParseEpochSec(lastEventTime_);
        if (diffSec > sessionIdleTimeoutSec_ || diffSec < 0) {
            sqlite3_stmt* closeStmt = nullptr;
            if (sqlite3_prepare_v2(db_,
                    "UPDATE sessions SET end_time = ?1, "
                    "duration_sec = (julianday(?1) - julianday(start_time)) * 86400 "
                    "WHERE id = ?2 AND end_time IS NULL",
                    -1, &closeStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(closeStmt, 1, lastEventTime_.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(closeStmt, 2, activeSessionId_);
                sqlite3_step(closeStmt);
                sqlite3_finalize(closeStmt);
            }
            ComputeSessionSummary(activeSessionId_);
            activeSessionId_ = -1;
        }
    }

    if (activeSessionId_ < 0) {
        sqlite3_stmt* sessStmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "INSERT INTO sessions (start_time) VALUES(?1)",
                -1, &sessStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(sessStmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(sessStmt);
            sqlite3_finalize(sessStmt);
        }
        activeSessionId_ = static_cast<int>(sqlite3_last_insert_rowid(db_));
    }

    lastEventTime_ = now;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "INSERT INTO interaction_events "
            "(entity_id, session_id, timestamp, event_type, duration_sec, depth, source) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, entityId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, activeSessionId_);
        sqlite3_bind_text(stmt, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, eventType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, durationSec);
        sqlite3_bind_text(stmt, 6, depth.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<InterestGraph::EntityInfo> InterestGraph::GetTopEntities(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EntityInfo> result;
    if (!db_) return result;

    std::string sql =
        "SELECT e.id, e.canonical_name, e.entity_type, COUNT(ev.id) as cnt "
        "FROM entities e "
        "LEFT JOIN interaction_events ev ON ev.entity_id = e.id "
        "GROUP BY e.id "
        "ORDER BY cnt DESC LIMIT " + std::to_string(limit);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EntityInfo info;
            info.id = SafeStr(sqlite3_column_text(stmt, 0));
            info.canonicalName = SafeStr(sqlite3_column_text(stmt, 1));
            info.entityType = SafeStr(sqlite3_column_text(stmt, 2));
            info.eventCount = sqlite3_column_int(stmt, 3);
            result.push_back(std::move(info));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

std::vector<InterestGraph::EventSummary> InterestGraph::GetRecentEventSummary(int days) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EventSummary> result;
    if (!db_) return result;

    const char* sql =
        "SELECT ev.entity_id, e.canonical_name, ev.event_type, "
        "COUNT(*) as cnt, SUM(ev.duration_sec) as total_dur "
        "FROM interaction_events ev "
        "JOIN entities e ON e.id = ev.entity_id "
        "WHERE ev.timestamp >= datetime('now', ?1) "
        "GROUP BY ev.entity_id, ev.event_type "
        "ORDER BY cnt DESC LIMIT 30";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        std::string offset = "-" + std::to_string(days) + " days";
        sqlite3_bind_text(stmt, 1, offset.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EventSummary s;
            s.entityId = SafeStr(sqlite3_column_text(stmt, 0));
            s.entityName = SafeStr(sqlite3_column_text(stmt, 1));
            s.eventType = SafeStr(sqlite3_column_text(stmt, 2));
            s.count = sqlite3_column_int(stmt, 3);
            s.totalDurationSec = sqlite3_column_int(stmt, 4);
            result.push_back(std::move(s));
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

int InterestGraph::GetEntityCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM entities", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

int InterestGraph::GetEventCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM interaction_events", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return count;
}

bool InterestGraph::IsValidEntityType(const std::string& type) const {
    return validEntityTypes_.empty() || validEntityTypes_.count(type) > 0;
}

bool InterestGraph::IsValidEventType(const std::string& type) const {
    return validEventTypes_.empty() || validEventTypes_.count(type) > 0;
}

bool InterestGraph::IsValidRelationType(const std::string& type) const {
    return validRelationTypes_.empty() || validRelationTypes_.count(type) > 0;
}

bool InterestGraph::IsValidDepth(const std::string& depth) const {
    return validDepths_.empty() || validDepths_.count(depth) > 0;
}

void InterestGraph::ComputeSessionSummary(int sessionId) {
    if (!db_ || sessionId < 0) return;

    int eventCount = 0;
    std::string dominantEntity;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT entity_id, COUNT(*) as cnt FROM interaction_events "
            "WHERE session_id = ?1 GROUP BY entity_id ORDER BY cnt DESC LIMIT 1",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, sessionId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            dominantEntity = SafeStr(sqlite3_column_text(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db_,
            "SELECT COUNT(*) FROM interaction_events WHERE session_id = ?1",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, sessionId);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            eventCount = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    if (sqlite3_prepare_v2(db_,
            "UPDATE sessions SET event_count = ?1, dominant_entity = ?2 WHERE id = ?3",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, eventCount);
        if (dominantEntity.empty()) {
            sqlite3_bind_null(stmt, 2);
        } else {
            sqlite3_bind_text(stmt, 2, dominantEntity.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int(stmt, 3, sessionId);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void InterestGraph::MergeEntities(const std::string& keepId, const std::string& removeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_ || keepId.empty() || removeId.empty() || keepId == removeId) return;

    sqlite3_stmt* stmt = nullptr;

    // Move aliases
    if (sqlite3_prepare_v2(db_,
            "UPDATE entity_aliases SET entity_id = ?1 WHERE entity_id = ?2",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, keepId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, removeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Move events
    if (sqlite3_prepare_v2(db_,
            "UPDATE interaction_events SET entity_id = ?1 WHERE entity_id = ?2",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, keepId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, removeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Move relations
    if (sqlite3_prepare_v2(db_,
            "UPDATE OR IGNORE relations SET from_id = ?1 WHERE from_id = ?2",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, keepId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, removeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    if (sqlite3_prepare_v2(db_,
            "UPDATE OR IGNORE relations SET to_id = ?1 WHERE to_id = ?2",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, keepId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, removeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Merge domain_weights
    std::string keepWeights = "{}";
    std::string removeWeights = "{}";
    if (sqlite3_prepare_v2(db_, "SELECT domain_weights FROM entities WHERE id = ?1",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, keepId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) keepWeights = SafeStr(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
    }
    if (sqlite3_prepare_v2(db_, "SELECT domain_weights FROM entities WHERE id = ?1",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, removeId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) removeWeights = SafeStr(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
    }
    std::string merged = MergeDomainWeightsJson(keepWeights, removeWeights);
    std::string now = GetCurrentTimeISO();
    if (sqlite3_prepare_v2(db_,
            "UPDATE entities SET domain_weights = ?1, updated_at = ?2, provisional = 0 WHERE id = ?3",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, merged.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, keepId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Delete removed entity
    if (sqlite3_prepare_v2(db_, "DELETE FROM entities WHERE id = ?1",
                           -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, removeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    // Clean orphaned relations
    if (sqlite3_prepare_v2(db_,
            "DELETE FROM relations WHERE from_id = ?1 OR to_id = ?1",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, removeId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Update alias cache
    for (auto& [alias, id] : aliasCache_) {
        if (id == removeId) id = keepId;
    }
}
