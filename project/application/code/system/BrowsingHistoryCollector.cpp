#include "BrowsingHistoryCollector.h"
#include "InterestGraph.h"
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
        "New Tab", "\xe6\x96\xb0\xe3\x81\x97\xe3\x81\x84\xe3\x82\xbf\xe3\x83\x96", // 新しいタブ
        "Google", "Bing",
        "about:blank", "Downloads", "Settings", "Extensions",
    };
    for (auto* n : kNoise) {
        if (title == n) return true;
    }
    return false;
}

// タブタイトルから通知バッジ "(数字)" を除去し、サイト名テンプレート部分を取り除く
std::string CleanTabTitle(const std::string& title) {
    std::string s = title;

    // 先頭の "(数字) " を除去 (例: "(3) Xユーザーの..." → "Xユーザーの...")
    if (s.size() > 2 && s[0] == '(') {
        size_t close = s.find(')');
        if (close != std::string::npos && close < 8) {
            bool allDigits = true;
            for (size_t i = 1; i < close; ++i) {
                if (s[i] < '0' || s[i] > '9') { allDigits = false; break; }
            }
            if (allDigits) {
                size_t start = close + 1;
                while (start < s.size() && s[start] == ' ') ++start;
                s = s.substr(start);
            }
        }
    }

    // 末尾の " - サービス名" を除去 (例: "動画タイトル - YouTube" → "動画タイトル")
    static const char* kSuffixes[] = {
        " - YouTube", " - X", " - Twitter",
        " - GitHub", " - Qiita", " - Zenn",
        " - note", " - Reddit", " - Wikipedia",
        " - Stack Overflow", " - Amazon",
        " \xc2\xb7 GitHub",  // " · GitHub"
    };
    for (auto* suf : kSuffixes) {
        size_t sufLen = std::strlen(suf);
        if (s.size() > sufLen && s.compare(s.size() - sufLen, sufLen, suf) == 0) {
            s = s.substr(0, s.size() - sufLen);
            break;
        }
    }

    // 末尾の空白除去
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

// 抽出されたキーワードがノイズかどうか判定
bool IsNoiseKeyword(const std::string& kw) {
    if (kw.empty() || kw.size() < 2) return true;
    // バッククォートのみ
    if (kw.find_first_not_of("` \t\r\n") == std::string::npos) return true;
    // 通知バッジの残骸
    if (kw[0] == '(' && kw.find(')') != std::string::npos && kw.size() < 6) return true;
    // サービス名そのもの
    static const char* kServiceNames[] = {
        "YouTube", "Twitter", "X\xe3\x83\xa6\xe3\x83\xbc\xe3\x82\xb6\xe3\x83\xbc", // Xユーザー
        "GitHub", "Google", "Amazon", "Reddit", "Wikipedia",
    };
    for (auto* sn : kServiceNames) {
        if (kw == sn) return true;
    }
    return false;
}

// Qwen3 の thinking mode 出力 <think>...</think> を除去
std::string StripThinkingTags(const std::string& s) {
    std::string result = s;
    while (true) {
        auto start = result.find("<think>");
        if (start == std::string::npos) break;
        auto end = result.find("</think>", start);
        if (end == std::string::npos) {
            result = result.substr(0, start);
            break;
        }
        result.erase(start, end + 8 - start);
    }
    return result;
}

}  // namespace

BrowsingHistoryCollector::BrowsingHistoryCollector() = default;
BrowsingHistoryCollector::~BrowsingHistoryCollector() = default;

void BrowsingHistoryCollector::LoadCategories(const std::string& tagAxesPath) {
    std::ifstream file(tagAxesPath);
    if (!file.is_open()) return;

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

    categories_.clear();

    // "interest_categories": ["音楽", "ゲーム", ...] を読む
    auto key = json.find("\"interest_categories\"");
    if (key == std::string::npos) return;

    auto arrStart = json.find('[', key);
    if (arrStart == std::string::npos) return;

    auto arrEnd = json.find(']', arrStart);
    if (arrEnd == std::string::npos) return;

    size_t pos = arrStart + 1;
    while (pos < arrEnd) {
        auto qStart = json.find('"', pos);
        if (qStart == std::string::npos || qStart >= arrEnd) break;
        auto qEnd = json.find('"', qStart + 1);
        if (qEnd == std::string::npos || qEnd >= arrEnd) break;
        categories_.push_back(json.substr(qStart + 1, qEnd - qStart - 1));
        pos = qEnd + 1;
    }
}

std::string BrowsingHistoryCollector::CategoryToSourceHint(const std::string& category) {
    // UTF-8 literal comparisons
    if (category == "\xe9\x9f\xb3\xe6\xa5\xbd") return "browser_history_music";         // 音楽
    if (category == "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0") return "browser_history_game"; // ゲーム
    if (category == "\xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1") return "browser_history_anime"; // アニメ
    if (category == "\xe6\xbc\xab\xe7\x94\xbb") return "browser_history_manga";          // 漫画
    if (category == "\xe5\x8b\x95\xe7\x94\xbb") return "browser_history_video";          // 動画
    if (category == "\xe3\x83\x97\xe3\x83\xad\xe3\x82\xb0\xe3\x83\xa9\xe3\x83\x9f\xe3\x83\xb3\xe3\x82\xb0") return "browser_history_programming"; // プログラミング
    if (category == "\xe6\x8a\x80\xe8\xa1\x93") return "browser_history_tech";           // 技術
    if (category == "\xe3\x82\xb9\xe3\x83\x9d\xe3\x83\xbc\xe3\x83\x84") return "browser_history_sports"; // スポーツ
    return "browser_history";
}

bool BrowsingHistoryCollector::IsValidCategory(const std::string& cat) const {
    if (categories_.empty()) return true;
    for (auto& c : categories_) {
        if (c == cat) return true;
    }
    return false;
}

auto BrowsingHistoryCollector::FindBrowserProfiles() const -> std::vector<BrowserProfile> {
    std::vector<BrowserProfile> profiles;
    std::string localApp = GetLocalAppData();
    if (localApp.empty()) return profiles;

    struct BrowserInfo {
        const char* name;
        const char* subPath;
    };
    // 各ブラウザの "User Data" ルート。配下の全プロファイル(Default / Profile 1 ...)を走査する
    // （Default 固定だと、別プロファイルで使っているブラウザの履歴を取りこぼす）。
    static const BrowserInfo kBrowsers[] = {
        {"Chrome",       "Google\\Chrome\\User Data"},
        {"Chrome Beta",  "Google\\Chrome Beta\\User Data"},
        {"Edge",         "Microsoft\\Edge\\User Data"},
        {"Brave",        "BraveSoftware\\Brave-Browser\\User Data"},
        {"Vivaldi",      "Vivaldi\\User Data"},
    };

    for (auto& b : kBrowsers) {
        std::string base = localApp + "\\" + b.subPath;
        if (!fs::exists(base)) {
            continue;
        }
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(base, ec)) {
            if (ec) break;
            if (!entry.is_directory()) {
                continue;
            }
            // プロファイルディレクトリ内に History DB があれば対象（Default, Profile *, Guest 等）。
            const std::string histPath = entry.path().string() + "\\History";
            if (fs::exists(histPath)) {
                profiles.push_back({b.name, histPath});
            }
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

    lastReport_ = CollectionReport{};

    for (auto& p : profiles) {
        BrowserCollectionInfo info;
        info.name = p.name;
        info.found = true;
        auto entries = ReadSqliteHistory(p.historyPath, maxEntries, daysBack);
        info.entryCount = static_cast<int>(entries.size());
        lastReport_.browsers.push_back(info);
        for (auto& e : entries) {
            e.browserName = p.name;
            all.push_back(std::move(e));
        }
    }

    // 検索対象だが見つからなかったブラウザも記録
    struct BrowserInfo {
        const char* name;
    };
    static const BrowserInfo kAllBrowsers[] = {
        {"Chrome"}, {"Chrome Beta"}, {"Edge"}, {"Brave"}, {"Vivaldi"},
    };
    for (auto& b : kAllBrowsers) {
        bool alreadyListed = false;
        for (auto& bi : lastReport_.browsers) {
            if (bi.name == b.name) { alreadyListed = true; break; }
        }
        if (!alreadyListed) {
            lastReport_.browsers.push_back({b.name, false, 0});
        }
    }

    lastReport_.totalEntries = static_cast<int>(all.size());

    std::sort(all.begin(), all.end(), [](const BrowsingEntry& a, const BrowsingEntry& b) {
        return a.lastVisit > b.lastVisit;
    });
    if (static_cast<int>(all.size()) > maxEntries) {
        all.resize(maxEntries);
    }
    lastRawEntries_ = all;
    return all;
}

std::string BrowsingHistoryCollector::BuildTitleList(
    const std::vector<BrowsingEntry>& entries) const {

    std::unordered_set<std::string> seen;
    std::ostringstream ss;
    int count = 0;
    for (auto& e : entries) {
        std::string cleaned = CleanTabTitle(e.title);
        if (cleaned.empty() || cleaned.size() < 3) continue;
        if (seen.count(cleaned)) continue;
        seen.insert(cleaned);
        ss << "- " << cleaned << "\n";
        if (++count >= 20) break;
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

    lastReport_.servicesFound = static_cast<int>(groups.size());
    lastReport_.phase = "ServiceKeywords";
    lastReport_.extractedKeywords.clear();
    lastReport_.rejectedLines.clear();
    lastReport_.llmRawOutput.clear();
    lastReport_.categoriesLoaded = static_cast<int>(categories_.size());

    // ブラウザ名 → プロセス名マッピング
    auto browserNameToProcess = [](const std::string& name) -> std::string {
        if (name == "Chrome" || name == "Chrome Beta") return "chrome.exe";
        if (name == "Edge") return "msedge.exe";
        if (name == "Brave") return "brave.exe";
        if (name == "Vivaldi") return "vivaldi.exe";
        return "";
    };

    // 各サービスの閲覧データをWebServiceRecordに登録
    for (auto& [service, serviceEntries] : groups) {
        // 「最後に使ったブラウザ」を採用する。visit_count は生涯累積値で、過去に多用した
        // ブラウザへ偏る（乗り換えても古いブラウザが勝ち続ける）ため、最近性で選ぶ。
        // lastVisit は "%Y-%m-%d %H:%M" 形式＝辞書順が時系列順なので文字列比較でよい。
        std::unordered_map<std::string, std::string> browserRecent; // browser -> 最新 lastVisit
        std::unordered_map<std::string, int> browserCounts;
        int totalVisits = 0;
        for (auto& e : serviceEntries) {
            if (!e.browserName.empty()) {
                browserCounts[e.browserName] += e.visitCount;
                auto& r = browserRecent[e.browserName];
                if (e.lastVisit > r) r = e.lastVisit;
            }
            totalVisits += e.visitCount;
        }
        std::string topBrowser;
        std::string topRecent;
        int topCount = 0;
        for (auto& [bname, recent] : browserRecent) {
            const int cnt = browserCounts[bname];
            // 直近に使ったブラウザを優先。同時刻なら訪問数の多い方。
            if (recent > topRecent || (recent == topRecent && cnt > topCount)) {
                topRecent = recent;
                topBrowser = bname;
                topCount = cnt;
            }
        }
        std::string browserProc = browserNameToProcess(topBrowser);
        memory->ImportWebServiceFromHistory(service, browserProc, totalVisits);
    }

    for (auto& [service, serviceEntries] : groups) {
        if (serviceEntries.size() < 3) continue;

        std::unordered_set<std::string> seen;
        std::ostringstream titleList;
        int count = 0;
        for (auto& e : serviceEntries) {
            std::string cleaned = CleanTabTitle(e.title);
            if (cleaned.empty() || cleaned.size() < 3) continue;
            if (seen.count(cleaned)) continue;
            seen.insert(cleaned);
            titleList << "- " << cleaned << "\n";
            if (++count >= 15) break;
        }

        std::string categoryList;
        for (size_t i = 0; i < categories_.size(); ++i) {
            if (i > 0) categoryList += ",";
            categoryList += categories_[i];
        }

        // system: 抽出ルールと出力形式
        // user: 実際のタイトル
        std::string systemPrompt =
            "\xe3\x82\xbf\xe3\x82\xa4\xe3\x83\x88\xe3\x83\xab\xe4\xb8\x80\xe8\xa6\xa7\xe3\x81\x8b\xe3\x82\x89"
            "\xe5\x9b\xba\xe6\x9c\x89\xe5\x90\x8d\xe8\xa9\x9e\xe3\x82\xad\xe3\x83\xbc\xe3\x83\xaf\xe3\x83\xbc\xe3\x83\x89"
            "\xe3\x82\x92\xe6\x8a\xbd\xe5\x87\xba\xe3\x81\x99\xe3\x82\x8b\xe3\x82\xa2\xe3\x82\xb7\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\xb3\xe3\x83\x88\xe3\x81\xa7\xe3\x81\x99\xe3\x80\x82\n"
            "\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe4\xb8\x80\xe8\xa6\xa7: "
            + categoryList + "\n"
            // 抽出ルール:
            // - 音楽: アーティスト名と楽曲名の両方を抽出
            // - アニメ/動画: 作品名を抽出。関連する音楽(OP/ED/主題歌)があればアーティスト名も音楽カテゴリに追加
            // - ドラマ・映画の作品名もアニメまたは動画カテゴリに分類
            // - 1つのタイトルが複数カテゴリにまたがる場合、それぞれに出力
            "\xe6\x8a\xbd\xe5\x87\xba\xe3\x83\xab\xe3\x83\xbc\xe3\x83\xab:\n"
            "- \xe9\x9f\xb3\xe6\xa5\xbd: \xe3\x82\xa2\xe3\x83\xbc\xe3\x83\x86\xe3\x82\xa3\xe3\x82\xb9\xe3\x83\x88\xe5\x90\x8d\xe3\x81\xa8\xe6\xa5\xbd\xe6\x9b\xb2\xe5\x90\x8d\xe3\x81\xae\xe4\xb8\xa1\xe6\x96\xb9\xe3\x82\x92\xe6\x8a\xbd\xe5\x87\xba\n"
            "- \xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1/\xe5\x8b\x95\xe7\x94\xbb: \xe4\xbd\x9c\xe5\x93\x81\xe5\x90\x8d\xe3\x82\x92\xe6\x8a\xbd\xe5\x87\xba\xe3\x80\x82"
            "\xe9\x96\xa2\xe9\x80\xa3\xe9\x9f\xb3\xe6\xa5\xbd(OP/ED)\xe3\x81\x8c\xe3\x81\x82\xe3\x82\x8c\xe3\x81\xb0\xe9\x9f\xb3\xe6\xa5\xbd\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe3\x81\xab\xe3\x82\x82\xe8\xbf\xbd\xe5\x8a\xa0\n"
            "- \xe3\x83\x89\xe3\x83\xa9\xe3\x83\x9e\xe3\x83\xbb\xe6\x98\xa0\xe7\x94\xbb\xe3\x81\xae\xe4\xbd\x9c\xe5\x93\x81\xe5\x90\x8d\xe3\x82\x82\xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1\xe3\x81\xbe\xe3\x81\x9f\xe3\x81\xaf\xe5\x8b\x95\xe7\x94\xbb\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe3\x81\xab\xe5\x88\x86\xe9\xa1\x9e\n"
            "- 1\xe3\x81\xa4\xe3\x81\xae\xe3\x82\xbf\xe3\x82\xa4\xe3\x83\x88\xe3\x83\xab\xe3\x81\x8c\xe8\xa4\x87\xe6\x95\xb0\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe3\x81\xab\xe3\x81\xbe\xe3\x81\x9f\xe3\x81\x8c\xe3\x82\x8b\xe5\xa0\xb4\xe5\x90\x88\xe3\x80\x81\xe3\x81\x9d\xe3\x82\x8c\xe3\x81\x9e\xe3\x82\x8c\xe3\x81\xab\xe5\x87\xba\xe5\x8a\x9b\n"
            "\xe5\x87\xba\xe5\x8a\x9b\xe3\x81\xaf\xe3\x80\x8c\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa:\xe3\x82\xad\xe3\x83\xbc\xe3\x83\xaf\xe3\x83\xbc\xe3\x83\x89"
            "1,\xe3\x82\xad\xe3\x83\xbc\xe3\x83\xaf\xe3\x83\xbc\xe3\x83\x89" "2\xe3\x80\x8d"
            "\xe3\x81\xae\xe5\xbd\xa2\xe5\xbc\x8f\xe3\x81\xae\xe3\x81\xbf\xe3\x80\x82"
            "\xe8\xaa\xac\xe6\x98\x8e\xe4\xb8\x8d\xe8\xa6\x81\xe3\x80\x82\n"
            "\xe4\xbe\x8b:\n"
            "\xe9\x9f\xb3\xe6\xa5\xbd:YOASOBI,\xe3\x82\xa2\xe3\x82\xa4\xe3\x83\x89\xe3\x83\xab,\xe5\xa4\x9c\xe3\x81\xab\xe9\xa7\x86\xe3\x81\x91\xe3\x82\x8b\n"
            "\xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1:\xe6\x8e\xa8\xe3\x81\x97\xe3\x81\xae\xe5\xad\x90,\xe5\x91\xaa\xe8\xa1\x93\xe5\xbb\xbb\xe6\x88\xa6\n"
            "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0:APEX,VALORANT\n"
            "\xe3\x83\x97\xe3\x83\xad\xe3\x82\xb0\xe3\x83\xa9\xe3\x83\x9f\xe3\x83\xb3\xe3\x82\xb0:React,C++\n";

        std::string userPrompt = service + "\xe3\x81\xae\xe3\x82\xbf\xe3\x82\xa4\xe3\x83\x88\xe3\x83\xab:\n"
            + titleList.str() + "/no_think";

        llm->SetMaxTokens(256);
        std::string rawResult = llm->GenerateChat(systemPrompt, userPrompt);
        if (rawResult.empty()) {
            lastReport_.rejectedLines.push_back("[" + service + "] LLM returned empty");
            continue;
        }
        if (!lastReport_.llmRawOutput.empty()) lastReport_.llmRawOutput += "\n---\n";
        lastReport_.llmRawOutput += "[" + service + "]\n" + rawResult;

        std::string result = StripThinkingTags(rawResult);

        // パース: "カテゴリ:kw1,kw2" 形式
        std::istringstream iss(result);
        std::string line;
        std::vector<std::string> allKeywords;
        std::vector<std::string> kwEntityIds;
        int addedCount = 0;
        while (std::getline(iss, line)) {
            size_t start = 0;
            while (start < line.size() && (line[start] == '-' || line[start] == ' ' ||
                   line[start] == '*' || line[start] == '\t')) {
                ++start;
            }
            line = line.substr(start);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                   line.back() == ' ')) {
                line.pop_back();
            }

            // 全角コロンを半角に正規化
            {
                std::string normalized;
                for (size_t i = 0; i < line.size(); ++i) {
                    if (i + 2 < line.size() &&
                        static_cast<unsigned char>(line[i]) == 0xEF &&
                        static_cast<unsigned char>(line[i+1]) == 0xBC &&
                        static_cast<unsigned char>(line[i+2]) == 0x9A) {
                        normalized += ':';
                        i += 2;
                    } else {
                        normalized += line[i];
                    }
                }
                line = normalized;
            }

            auto colonPos = line.find(':');
            if (colonPos == std::string::npos) continue;

            std::string category = line.substr(0, colonPos);
            std::string rest = line.substr(colonPos + 1);
            if (category.empty() || rest.empty()) continue;
            if (!IsValidCategory(category)) {
                lastReport_.rejectedLines.push_back("bad cat: " + category + " -> " + rest);
                continue;
            }

            std::string sourceHint = CategoryToSourceHint(category);

            std::istringstream kwStream(rest);
            std::string kw;
            while (std::getline(kwStream, kw, ',')) {
                while (!kw.empty() && kw.front() == ' ') kw.erase(0, 1);
                while (!kw.empty() && kw.back() == ' ') kw.pop_back();
                while (!kw.empty() && kw.front() == '#') kw.erase(0, 1);
                if (!IsNoiseKeyword(kw) && kw.size() < 50 && !kw.empty()) {
                    memory->AddInterest(category, kw);
                    allKeywords.push_back(kw);
                    ++addedCount;

                    if (ig_) {
                        std::string eid = ig_->ResolveEntity(kw, sourceHint);
                        if (!eid.empty()) {
                            ig_->RecordEvent(eid, "viewed", 0, "browse", sourceHint);
                            kwEntityIds.push_back(eid);
                        }
                    }
                }
            }
        }

        if (!allKeywords.empty()) {
            std::ostringstream content;
            content << service << "\xe3\x81\xa7\xe3\x81\xae\xe9\x96\xa2\xe5\xbf\x83: ";
            for (size_t i = 0; i < allKeywords.size(); ++i) {
                if (i > 0) content << ", ";
                content << allKeywords[i];
            }
            memory->AddFact("\xe3\x82\xb5\xe3\x83\xbc\xe3\x83\x93\xe3\x82\xb9\xe5\x88\xa5\xe9\x96\xa2\xe5\xbf\x83_" + service, content.str(), 1.2f);
            lastCollectedCount_ += addedCount;

            for (auto& kw : allKeywords) {
                lastReport_.extractedKeywords.push_back(service + ": " + kw);
            }

            if (ig_ && !kwEntityIds.empty()) {
                std::string svcEntityId = ig_->ResolveEntity(service, "browser_service");
                if (!svcEntityId.empty()) {
                    for (auto& kwEid : kwEntityIds) {
                        ig_->AddRelation(kwEid, svcEntityId, "belongs_to", 1.0f, "browser_history");
                    }
                }
            }
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

    std::string categoryList;
    for (size_t i = 0; i < categories_.size(); ++i) {
        if (i > 0) categoryList += ",";
        categoryList += categories_[i];
    }

    std::string systemPrompt =
        "\xe3\x82\xbf\xe3\x82\xa4\xe3\x83\x88\xe3\x83\xab\xe4\xb8\x80\xe8\xa6\xa7\xe3\x81\x8b\xe3\x82\x89"
        "\xe5\x9b\xba\xe6\x9c\x89\xe5\x90\x8d\xe8\xa9\x9e\xe3\x82\xad\xe3\x83\xbc\xe3\x83\xaf\xe3\x83\xbc\xe3\x83\x89"
        "\xe3\x82\x92\xe6\x8a\xbd\xe5\x87\xba\xe3\x81\x99\xe3\x82\x8b\xe3\x82\xa2\xe3\x82\xb7\xe3\x82\xb9\xe3\x82\xbf\xe3\x83\xb3\xe3\x83\x88\xe3\x81\xa7\xe3\x81\x99\xe3\x80\x82\n"
        "\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe4\xb8\x80\xe8\xa6\xa7: "
        + categoryList + "\n"
        "\xe6\x8a\xbd\xe5\x87\xba\xe3\x83\xab\xe3\x83\xbc\xe3\x83\xab:\n"
        "- \xe9\x9f\xb3\xe6\xa5\xbd: \xe3\x82\xa2\xe3\x83\xbc\xe3\x83\x86\xe3\x82\xa3\xe3\x82\xb9\xe3\x83\x88\xe5\x90\x8d\xe3\x81\xa8\xe6\xa5\xbd\xe6\x9b\xb2\xe5\x90\x8d\xe3\x81\xae\xe4\xb8\xa1\xe6\x96\xb9\xe3\x82\x92\xe6\x8a\xbd\xe5\x87\xba\n"
        "- \xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1/\xe5\x8b\x95\xe7\x94\xbb: \xe4\xbd\x9c\xe5\x93\x81\xe5\x90\x8d\xe3\x82\x92\xe6\x8a\xbd\xe5\x87\xba\xe3\x80\x82"
        "\xe9\x96\xa2\xe9\x80\xa3\xe9\x9f\xb3\xe6\xa5\xbd(OP/ED)\xe3\x81\x8c\xe3\x81\x82\xe3\x82\x8c\xe3\x81\xb0\xe9\x9f\xb3\xe6\xa5\xbd\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe3\x81\xab\xe3\x82\x82\xe8\xbf\xbd\xe5\x8a\xa0\n"
        "- \xe3\x83\x89\xe3\x83\xa9\xe3\x83\x9e\xe3\x83\xbb\xe6\x98\xa0\xe7\x94\xbb\xe3\x81\xae\xe4\xbd\x9c\xe5\x93\x81\xe5\x90\x8d\xe3\x82\x82\xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1\xe3\x81\xbe\xe3\x81\x9f\xe3\x81\xaf\xe5\x8b\x95\xe7\x94\xbb\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe3\x81\xab\xe5\x88\x86\xe9\xa1\x9e\n"
        "- 1\xe3\x81\xa4\xe3\x81\xae\xe3\x82\xbf\xe3\x82\xa4\xe3\x83\x88\xe3\x83\xab\xe3\x81\x8c\xe8\xa4\x87\xe6\x95\xb0\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa\xe3\x81\xab\xe3\x81\xbe\xe3\x81\x9f\xe3\x81\x8c\xe3\x82\x8b\xe5\xa0\xb4\xe5\x90\x88\xe3\x80\x81\xe3\x81\x9d\xe3\x82\x8c\xe3\x81\x9e\xe3\x82\x8c\xe3\x81\xab\xe5\x87\xba\xe5\x8a\x9b\n"
        "\xe5\x87\xba\xe5\x8a\x9b\xe3\x81\xaf\xe3\x80\x8c\xe3\x82\xab\xe3\x83\x86\xe3\x82\xb4\xe3\x83\xaa:\xe3\x82\xad\xe3\x83\xbc\xe3\x83\xaf\xe3\x83\xbc\xe3\x83\x89"
        "1,\xe3\x82\xad\xe3\x83\xbc\xe3\x83\xaf\xe3\x83\xbc\xe3\x83\x89" "2\xe3\x80\x8d"
        "\xe3\x81\xae\xe5\xbd\xa2\xe5\xbc\x8f\xe3\x81\xae\xe3\x81\xbf\xe3\x80\x82"
        "\xe8\xaa\xac\xe6\x98\x8e\xe4\xb8\x8d\xe8\xa6\x81\xe3\x80\x82\n"
        "\xe4\xbe\x8b:\n"
        "\xe9\x9f\xb3\xe6\xa5\xbd:YOASOBI,\xe3\x82\xa2\xe3\x82\xa4\xe3\x83\x89\xe3\x83\xab,\xe5\xa4\x9c\xe3\x81\xab\xe9\xa7\x86\xe3\x81\x91\xe3\x82\x8b\n"
        "\xe3\x82\xa2\xe3\x83\x8b\xe3\x83\xa1:\xe6\x8e\xa8\xe3\x81\x97\xe3\x81\xae\xe5\xad\x90,\xe5\x91\xaa\xe8\xa1\x93\xe5\xbb\xbb\xe6\x88\xa6\n"
        "\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0:APEX,VALORANT\n"
        "\xe3\x83\x97\xe3\x83\xad\xe3\x82\xb0\xe3\x83\xa9\xe3\x83\x9f\xe3\x83\xb3\xe3\x82\xb0:React,C++\n";

    std::string userPrompt =
        "\xe3\x82\xbf\xe3\x82\xa4\xe3\x83\x88\xe3\x83\xab:\n" + titles + "/no_think";

    llm->SetMaxTokens(256);
    std::string rawResult = llm->GenerateChat(systemPrompt, userPrompt);

    if (rawResult.empty()) {
        lastError_ = "LLM returned empty result";
        isCollecting_ = false;
        return;
    }

    lastReport_.phase = "Interests";
    lastReport_.extractedKeywords.clear();
    lastReport_.rejectedLines.clear();
    lastReport_.llmRawOutput = rawResult;
    lastReport_.categoriesLoaded = static_cast<int>(categories_.size());

    std::string result = StripThinkingTags(rawResult);

    // パース: "カテゴリ:kw1,kw2" 形式
    std::istringstream iss(result);
    std::string line;
    int addedCount = 0;
    while (std::getline(iss, line)) {
        // 箇条書きプレフィックスを除去
        size_t start = 0;
        while (start < line.size() && (line[start] == '-' || line[start] == ' ' ||
               line[start] == '*' || line[start] == '\t')) {
            ++start;
        }
        line = line.substr(start);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
               line.back() == ' ')) {
            line.pop_back();
        }

        // 全角コロンを半角に正規化
        {
            std::string normalized;
            for (size_t i = 0; i < line.size(); ++i) {
                if (i + 2 < line.size() &&
                    static_cast<unsigned char>(line[i]) == 0xEF &&
                    static_cast<unsigned char>(line[i+1]) == 0xBC &&
                    static_cast<unsigned char>(line[i+2]) == 0x9A) {
                    normalized += ':';
                    i += 2;
                } else {
                    normalized += line[i];
                }
            }
            line = normalized;
        }

        auto colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        std::string category = line.substr(0, colonPos);
        std::string rest = line.substr(colonPos + 1);
        if (category.empty() || rest.empty()) continue;
        if (!IsValidCategory(category)) {
            lastReport_.rejectedLines.push_back("bad cat: " + category + " -> " + rest);
            continue;
        }

        std::string sourceHint = CategoryToSourceHint(category);

        std::istringstream kwStream(rest);
        std::string kw;
        while (std::getline(kwStream, kw, ',')) {
            while (!kw.empty() && kw.front() == ' ') kw.erase(0, 1);
            while (!kw.empty() && kw.back() == ' ') kw.pop_back();
            if (!IsNoiseKeyword(kw) && kw.size() < 50) {
                memory->AddInterest(category, kw);
                lastReport_.extractedKeywords.push_back(category + ": " + kw);
                ++addedCount;

                if (ig_) {
                    std::string eid = ig_->ResolveEntity(kw, sourceHint);
                    if (!eid.empty()) {
                        ig_->RecordEvent(eid, "viewed", 0, "browse", sourceHint);
                    }
                }
            }
        }
    }

    lastCollectedCount_ = addedCount;
    isCollecting_ = false;
}
