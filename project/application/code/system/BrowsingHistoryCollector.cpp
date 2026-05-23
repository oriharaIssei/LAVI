#include "BrowsingHistoryCollector.h"
#include "LocalLLM.h"
#include "LongTermMemory.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#ifdef _WIN32
#include <ShlObj.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string GetLocalAppData() {
#ifdef _WIN32
    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        std::string result(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr, nullptr);
        CoTaskMemFree(path);
        return result;
    }
#endif
    return "";
}

std::string CopyToTemp(const std::string& srcPath) {
#ifdef _WIN32
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    int len = WideCharToMultiByte(CP_UTF8, 0, tempDir, -1, nullptr, 0, nullptr, nullptr);
    std::string tmpDir(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, tempDir, -1, tmpDir.data(), len, nullptr, nullptr);
    std::string dest = tmpDir + "lavi_browser_history_tmp.db";
#else
    std::string dest = "/tmp/lavi_browser_history_tmp.db";
#endif
    std::error_code ec;
    fs::copy_file(srcPath, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) return "";
    return dest;
}

// Chrome の last_visit_time は 1601-01-01 からのマイクロ秒
// Unix epoch (1970-01-01) との差: 11644473600 秒
int64_t ChromeTimeToUnix(int64_t chromeTime) {
    return chromeTime / 1'000'000 - 11'644'473'600LL;
}

std::string UnixTimeToString(int64_t unixTime) {
    time_t t = static_cast<time_t>(unixTime);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
    return buf;
}

bool IsNoiseTitle(const std::string& title) {
    if (title.empty()) return true;
    if (title.size() < 3) return true;
    static const char* kNoise[] = {
        "New Tab", "新しいタブ", "Google", "Bing",
        "about:blank", "Downloads", "Settings", "Extensions",
    };
    for (auto* n : kNoise) {
        if (title == n) return true;
    }
    return false;
}

}  // namespace

BrowsingHistoryCollector::BrowsingHistoryCollector() = default;
BrowsingHistoryCollector::~BrowsingHistoryCollector() = default;

auto BrowsingHistoryCollector::FindBrowserProfiles() const -> std::vector<BrowserProfile> {
    std::vector<BrowserProfile> profiles;
    std::string localApp = GetLocalAppData();
    if (localApp.empty()) return profiles;

    struct BrowserInfo {
        const char* name;
        const char* subPath;
    };
    static const BrowserInfo kBrowsers[] = {
        {"Chrome",       "Google\\Chrome\\User Data\\Default\\History"},
        {"Chrome Beta",  "Google\\Chrome Beta\\User Data\\Default\\History"},
        {"Edge",         "Microsoft\\Edge\\User Data\\Default\\History"},
        {"Brave",        "BraveSoftware\\Brave-Browser\\User Data\\Default\\History"},
        {"Vivaldi",      "Vivaldi\\User Data\\Default\\History"},
    };

    for (auto& b : kBrowsers) {
        std::string path = localApp + "\\" + b.subPath;
        if (fs::exists(path)) {
            profiles.push_back({b.name, path});
        }
    }
    return profiles;
}

std::vector<BrowsingEntry> BrowsingHistoryCollector::ReadSqliteHistory(
    const std::string& dbPath, int maxEntries, int daysBack) const {

    std::vector<BrowsingEntry> entries;

    std::string tmpPath = CopyToTemp(dbPath);
    if (tmpPath.empty()) return entries;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(tmpPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        fs::remove(tmpPath);
        return entries;
    }

    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::hours(24 * daysBack);
    int64_t cutoffUnix = std::chrono::duration_cast<std::chrono::seconds>(
        cutoff.time_since_epoch()).count();
    int64_t cutoffChrome = (cutoffUnix + 11'644'473'600LL) * 1'000'000LL;

    std::string sql =
        "SELECT url, title, visit_count, last_visit_time "
        "FROM urls "
        "WHERE last_visit_time > ? AND hidden = 0 "
        "ORDER BY last_visit_time DESC "
        "LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, cutoffChrome);
        sqlite3_bind_int(stmt, 2, maxEntries);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BrowsingEntry e;
            const char* url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            e.url = url ? url : "";
            e.title = title ? title : "";
            e.visitCount = sqlite3_column_int(stmt, 2);
            int64_t chromeTime = sqlite3_column_int64(stmt, 3);
            e.lastVisit = UnixTimeToString(ChromeTimeToUnix(chromeTime));

            if (!IsNoiseTitle(e.title)) {
                entries.push_back(std::move(e));
            }
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    std::error_code ec;
    fs::remove(tmpPath, ec);

    return entries;
}

std::vector<BrowsingEntry> BrowsingHistoryCollector::ReadRecentHistory(
    int maxEntries, int daysBack) const {

    std::vector<BrowsingEntry> all;
    auto profiles = FindBrowserProfiles();

    for (auto& p : profiles) {
        auto entries = ReadSqliteHistory(p.historyPath, maxEntries, daysBack);
        for (auto& e : entries) {
            all.push_back(std::move(e));
        }
    }

    std::sort(all.begin(), all.end(), [](const BrowsingEntry& a, const BrowsingEntry& b) {
        return a.lastVisit > b.lastVisit;
    });
    if (static_cast<int>(all.size()) > maxEntries) {
        all.resize(maxEntries);
    }
    return all;
}

std::string BrowsingHistoryCollector::BuildTitleList(
    const std::vector<BrowsingEntry>& entries) const {

    std::unordered_set<std::string> seen;
    std::ostringstream ss;
    int count = 0;
    for (auto& e : entries) {
        if (seen.count(e.title)) continue;
        seen.insert(e.title);
        ss << "- " << e.title << "\n";
        if (++count >= 50) break;
    }
    return ss.str();
}

std::string BrowsingHistoryCollector::ExtractServiceFromUrl(const std::string& url) {
    struct UrlPattern {
        const char* domain;
        const char* serviceName;
    };
    static const UrlPattern kPatterns[] = {
        {"youtube.com",      "YouTube"},
        {"youtu.be",         "YouTube"},
        {"twitter.com",      "Twitter/X"},
        {"x.com",            "Twitter/X"},
        {"github.com",       "GitHub"},
        {"stackoverflow.com","StackOverflow"},
        {"reddit.com",       "Reddit"},
        {"qiita.com",        "Qiita"},
        {"zenn.dev",         "Zenn"},
        {"note.com",         "note"},
        {"hatena",           "はてな"},
        {"amazon.co.jp",     "Amazon"},
        {"amazon.com",       "Amazon"},
        {"wikipedia.org",    "Wikipedia"},
        {"twitch.tv",        "Twitch"},
        {"nicovideo.jp",     "niconico"},
        {"pixiv.net",        "Pixiv"},
        {"instagram.com",    "Instagram"},
        {"facebook.com",     "Facebook"},
        {"connpass.com",     "Connpass"},
        {"atcoder.jp",       "AtCoder"},
        {"4gamer.net",       "4Gamer"},
        {"docswell.com",     "Docswell"},
        {"speakerdeck.com",  "SpeakerDeck"},
        {"docs.google.com",  "GoogleDocs"},
        {"drive.google.com", "GoogleDrive"},
        {"mail.google.com",  "Gmail"},
        {"chatgpt.com",      "ChatGPT"},
        {"claude.ai",        "Claude"},
    };

    for (auto& p : kPatterns) {
        if (url.find(p.domain) != std::string::npos) {
            return p.serviceName;
        }
    }
    return "";
}

auto BrowsingHistoryCollector::GroupByService(
    const std::vector<BrowsingEntry>& entries) const -> ServiceGroup {
    ServiceGroup groups;
    for (auto& e : entries) {
        std::string service = ExtractServiceFromUrl(e.url);
        if (!service.empty()) {
            groups[service].push_back(e);
        }
    }
    return groups;
}

void BrowsingHistoryCollector::CollectServiceKeywords(
    LongTermMemory* memory, LocalLLM* llm, int daysBack) {
    if (!memory || !llm || !llm->IsModelLoaded()) return;
    if (isCollecting_) return;

    isCollecting_ = true;
    lastError_.clear();
    lastCollectedCount_ = 0;

    auto entries = ReadRecentHistory(1000, daysBack);
    if (entries.empty()) {
        lastError_ = "No browsing history found";
        isCollecting_ = false;
        return;
    }

    auto groups = GroupByService(entries);
    if (groups.empty()) {
        lastError_ = "No known services in history";
        isCollecting_ = false;
        return;
    }

    for (auto& [service, serviceEntries] : groups) {
        if (serviceEntries.size() < 3) continue;

        std::unordered_set<std::string> seen;
        std::ostringstream titleList;
        int count = 0;
        for (auto& e : serviceEntries) {
            if (seen.count(e.title)) continue;
            seen.insert(e.title);
            titleList << "- " << e.title << "\n";
            if (++count >= 30) break;
        }

        std::string prompt =
            "以下は「" + service + "」でのユーザーの閲覧タイトル一覧です。\n"
            "この人がこのサービスで何に興味を持っているか、頻出するテーマやキーワードを5〜8個抽出してください。\n"
            "キーワードのみを改行区切りで出力。余計な説明は不要です。\n\n"
            + titleList.str();

        llm->SetMaxTokens(256);
        std::string result = llm->Generate(prompt);
        if (result.empty()) continue;

        std::istringstream iss(result);
        std::string line;
        std::vector<std::string> keywords;
        while (std::getline(iss, line)) {
            size_t start = 0;
            while (start < line.size() && (line[start] == '-' || line[start] == ' ' ||
                   line[start] == '*' || line[start] == '\t')) {
                ++start;
            }
            if (start < line.size() && line.size() >= start + 3) {
                unsigned char c0 = static_cast<unsigned char>(line[start]);
                unsigned char c1 = static_cast<unsigned char>(line[start + 1]);
                unsigned char c2 = static_cast<unsigned char>(line[start + 2]);
                if (c0 == 0xE3 && c1 == 0x83 && c2 == 0xBB) {
                    start += 3;
                }
            }
            std::string kw = line.substr(start);
            while (!kw.empty() && (kw.back() == ' ' || kw.back() == '\r' ||
                   kw.back() == '\n' || kw.back() == '\t')) {
                kw.pop_back();
            }
            if (!kw.empty() && kw.size() < 50) {
                keywords.push_back(kw);
            }
        }

        if (!keywords.empty()) {
            std::ostringstream content;
            content << service << "での関心: ";
            for (size_t i = 0; i < keywords.size(); ++i) {
                if (i > 0) content << ", ";
                content << keywords[i];
            }
            memory->AddFact("サービス別関心_" + service, content.str(), 1.2f);
            ++lastCollectedCount_;
        }
    }

    isCollecting_ = false;
}

void BrowsingHistoryCollector::CollectInterests(LongTermMemory* memory, LocalLLM* llm) {
    if (!memory || !llm || !llm->IsModelLoaded()) return;
    if (isCollecting_) return;

    isCollecting_ = true;
    lastError_.clear();
    lastCollectedCount_ = 0;

    auto entries = ReadRecentHistory(200, 7);
    if (entries.empty()) {
        lastError_ = "No browsing history found";
        isCollecting_ = false;
        return;
    }

    std::string titles = BuildTitleList(entries);
    if (titles.empty()) {
        lastError_ = "No meaningful titles";
        isCollecting_ = false;
        return;
    }

    std::string prompt =
        "以下はユーザーのブラウザ閲覧履歴のタイトル一覧です。\n"
        "この人の趣味や興味を5〜10個のキーワードで抽出してください。\n"
        "キーワードのみを改行区切りで出力してください。説明は不要です。\n\n"
        + titles;

    llm->SetMaxTokens(256);
    std::string result = llm->Generate(prompt);

    if (result.empty()) {
        lastError_ = "LLM returned empty result";
        isCollecting_ = false;
        return;
    }

    std::istringstream iss(result);
    std::string line;
    std::vector<std::string> interests;
    while (std::getline(iss, line)) {
        // "- " や "・" などの箇条書きプレフィックスを除去
        size_t start = 0;
        while (start < line.size() && (line[start] == '-' || line[start] == ' ' ||
               line[start] == '*' || line[start] == '\t')) {
            ++start;
        }
        // UTF-8 の "・" (0xE3 0x83 0xBB)
        if (start < line.size() && line.size() >= start + 3) {
            unsigned char c0 = static_cast<unsigned char>(line[start]);
            unsigned char c1 = static_cast<unsigned char>(line[start + 1]);
            unsigned char c2 = static_cast<unsigned char>(line[start + 2]);
            if (c0 == 0xE3 && c1 == 0x83 && c2 == 0xBB) {
                start += 3;
            }
        }
        std::string keyword = line.substr(start);
        // 末尾の空白を除去
        while (!keyword.empty() && (keyword.back() == ' ' || keyword.back() == '\r' ||
               keyword.back() == '\n' || keyword.back() == '\t')) {
            keyword.pop_back();
        }
        if (!keyword.empty() && keyword.size() < 50) {
            interests.push_back(keyword);
        }
    }

    if (!interests.empty()) {
        auto& profile = memory->GetUserProfile();
        for (auto& interest : interests) {
            bool alreadyExists = false;
            for (auto& existing : profile.interests) {
                if (existing == interest) {
                    alreadyExists = true;
                    break;
                }
            }
            if (!alreadyExists) {
                profile.interests.push_back(interest);
                ++lastCollectedCount_;
            }
        }

        memory->AddFact("browsing_interests",
            "Browsing analysis: " + std::to_string(interests.size()) + " interests extracted",
            0.8f);
    }

    isCollecting_ = false;
}
