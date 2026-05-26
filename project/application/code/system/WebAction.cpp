#include "WebAction.h"
#include "LongTermMemory.h"
#include "SentenceEmbedding.h"

#include <Windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <unordered_map>

static const char kTag[] = "[browser:";
static const size_t kTagLen = sizeof(kTag) - 1;

static std::string ExtractDomain(const std::string& url) {
    size_t start = url.find("://");
    if (start == std::string::npos) return "";
    start += 3;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    std::string host = url.substr(start, end - start);
    size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    return host;
}

struct DomainService {
    const char* domain;
    const char* service;
};

static const DomainService kDomainMap[] = {
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
    {"amazon.co.jp",     "Amazon"},
    {"amazon.com",       "Amazon"},
    {"wikipedia.org",    "Wikipedia"},
    {"twitch.tv",        "Twitch"},
    {"nicovideo.jp",     "niconico"},
    {"pixiv.net",        "Pixiv"},
    {"instagram.com",    "Instagram"},
    {"facebook.com",     "Facebook"},
    {"open.spotify.com", "Spotify"},
    {"spotify.com",      "Spotify"},
};

static std::string DomainToService(const std::string& domain) {
    for (const auto& e : kDomainMap) {
        if (domain == e.domain || (domain.size() > strlen(e.domain) + 1 &&
            domain.compare(domain.size() - strlen(e.domain) - 1,
                           strlen(e.domain) + 1,
                           std::string(".") + e.domain) == 0)) {
            return e.service;
        }
    }
    return "";
}

static std::wstring FindBrowserExe(const std::string& processName) {
    if (processName.empty()) return L"";

    int wlen = MultiByteToWideChar(CP_UTF8, 0, processName.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring wname(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, processName.c_str(), -1, wname.data(), wlen);

    // App Paths registry lookup
    std::wstring regKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\";
    regKey += wname.c_str();

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = sizeof(path);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            std::wstring result = path;
            if (!result.empty() && result.front() == L'"') result.erase(0, 1);
            if (!result.empty() && result.back() == L'"') result.pop_back();
            return result;
        }
        RegCloseKey(hKey);
    }

    // Fallback: HKCU
    if (RegOpenKeyExW(HKEY_CURRENT_USER, regKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t path[MAX_PATH] = {};
        DWORD size = sizeof(path);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            std::wstring result = path;
            if (!result.empty() && result.front() == L'"') result.erase(0, 1);
            if (!result.empty() && result.back() == L'"') result.pop_back();
            return result;
        }
        RegCloseKey(hKey);
    }

    return L"";
}

static void OpenUrl(const std::string& url, const std::wstring& browserExe) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return;
    std::wstring wurl(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);

    if (!browserExe.empty()) {
        ShellExecuteW(nullptr, L"open", browserExe.c_str(), wurl.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

static void AutoClickFirstResult(int delayMs) {
    std::thread([delayMs]() {
        Sleep(delayMs);

        HWND hwnd = GetForegroundWindow();
        if (!hwnd) return;

        RECT rect;
        if (!GetWindowRect(hwnd, &rect)) return;

        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;

        // 検索結果ページの最初のコンテンツ領域をクリック
        int clickX = rect.left + w * 35 / 100;
        int clickY = rect.top + h * 45 / 100;

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        INPUT inputs[3] = {};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dx = clickX * 65535 / screenW;
        inputs[0].mi.dy = clickY * 65535 / screenH;
        inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dx = inputs[0].mi.dx;
        inputs[1].mi.dy = inputs[0].mi.dy;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;

        inputs[2].type = INPUT_MOUSE;
        inputs[2].mi.dx = inputs[0].mi.dx;
        inputs[2].mi.dy = inputs[0].mi.dy;
        inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;

        SendInput(3, inputs, sizeof(INPUT));
    }).detach();
}

std::vector<WebAction> ParseWebActions(const std::string& text) {
    std::vector<WebAction> actions;
    size_t pos = 0;
    while ((pos = text.find(kTag, pos)) != std::string::npos) {
        size_t urlStart = pos + kTagLen;
        size_t urlEnd = text.find(']', urlStart);
        if (urlEnd == std::string::npos) break;

        std::string url = text.substr(urlStart, urlEnd - urlStart);
        if (url.find("http://") == 0 || url.find("https://") == 0) {
            actions.push_back({std::move(url)});
        }
        pos = urlEnd + 1;
    }
    return actions;
}

std::string StripWebActions(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        size_t tagPos = text.find(kTag, pos);
        if (tagPos == std::string::npos) {
            result.append(text, pos, text.size() - pos);
            break;
        }
        result.append(text, pos, tagPos - pos);
        size_t end = text.find(']', tagPos);
        pos = (end != std::string::npos) ? end + 1 : text.size();
    }
    return result;
}

void ExecuteWebActions(const std::vector<WebAction>& actions, LongTermMemory* memory) {
    for (const auto& a : actions) {
        std::wstring browserExe;

        if (memory) {
            std::string domain = ExtractDomain(a.url);
            std::string service = DomainToService(domain);
            if (!service.empty()) {
                const WebServiceRecord* rec = memory->FindWebService(service);
                if (rec && !rec->browserProcess.empty()) {
                    browserExe = FindBrowserExe(rec->browserProcess);
                }
            }
        }

        OpenUrl(a.url, browserExe);
    }
}

// --- ローカルアクション ---

namespace {

// --- ランタイムタグ語彙 (tag_axes.json から読み込み) ---
struct TagAxesData {
    std::vector<std::string> axisNames;
    std::vector<std::vector<std::string>> axisTagLists;
    std::unordered_map<std::string, int> tagToAxis;
    std::vector<std::string> allTags;
    std::unordered_map<std::string, std::string> synonyms;
    std::vector<std::string> interestCategories;
    bool loaded = false;
};

static TagAxesData g_tagAxes;

static std::vector<std::string> ParseJsonStringArray(const std::string& arr) {
    std::vector<std::string> result;
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
}

// サービス名 → 検索URL（カテゴリ非依存、純粋にURL構造の知識）
struct ServiceUrl {
    const char* name;
    const char* searchFmt;  // %s = URLエンコード済みクエリ
};

static const ServiceUrl kServiceUrls[] = {
    {"YouTube",       "https://www.youtube.com/results?search_query=%s"},
    {"Spotify",       "https://open.spotify.com/search/%s"},
    {"niconico",      "https://www.nicovideo.jp/search/%s"},
    {"Twitch",        "https://www.twitch.tv/search?term=%s"},
    {"Twitter/X",     "https://x.com/search?q=%s"},
    {"GitHub",        "https://github.com/search?q=%s"},
    {"Amazon",        "https://www.amazon.co.jp/s?k=%s"},
    {"Qiita",         "https://qiita.com/search?q=%s"},
    {"Zenn",          "https://zenn.dev/search?q=%s"},
    {"note",          "https://note.com/search?q=%s"},
    {"StackOverflow", "https://stackoverflow.com/search?q=%s"},
    {"Reddit",        "https://www.reddit.com/search/?q=%s"},
    {"Wikipedia",     "https://ja.wikipedia.org/w/index.php?search=%s"},
    {"Pixiv",         "https://www.pixiv.net/tags/%s"},
    {"Connpass",      "https://connpass.com/search/?q=%s"},
    {"4Gamer",        "https://www.4gamer.net/script/search/index.php?key=%s"},
    {"Docswell",      "https://www.docswell.com/search?q=%s"},
    {"\xe3\x81\xaf\xe3\x81\xa6\xe3\x81\xaa", "https://b.hatena.ne.jp/search/text?q=%s"},
    {"Google",        "https://www.google.com/search?q=%s"},
};

static std::string UrlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static std::string FormatUrl(const char* fmt, const std::string& query) {
    std::string encoded = UrlEncode(query);
    std::string url;
    for (const char* p = fmt; *p; ++p) {
        if (*p == '%' && *(p + 1) == 's') {
            url += encoded;
            ++p;
        } else {
            url += *p;
        }
    }
    return url;
}

static std::vector<std::string> ExtractIntentTags(const std::string& speech) {
    std::vector<std::string> tags;
    auto addUnique = [&](const std::string& tag) {
        for (auto& t : tags) if (t == tag) return;
        tags.push_back(tag);
    };
    for (auto& tag : g_tagAxes.allTags) {
        if (speech.find(tag) != std::string::npos) addUnique(tag);
    }
    for (auto& [word, tag] : g_tagAxes.synonyms) {
        if (speech.find(word) != std::string::npos) addUnique(tag);
    }
    return tags;
}

static int GetTagAxis(const std::string& tag) {
    auto it = g_tagAxes.tagToAxis.find(tag);
    return (it != g_tagAxes.tagToAxis.end()) ? it->second : -1;
}

static int ScoreByAxis(const std::vector<std::string>& intentTags,
                       const std::vector<std::string>& serviceTags) {
    int numAxes = static_cast<int>(g_tagAxes.axisNames.size());
    if (numAxes == 0) return 0;

    std::vector<std::vector<std::string>> intentByAxis(numAxes), serviceByAxis(numAxes);
    for (auto& t : intentTags) {
        int axis = GetTagAxis(t);
        if (axis >= 0 && axis < numAxes) intentByAxis[axis].push_back(t);
    }
    for (auto& t : serviceTags) {
        int axis = GetTagAxis(t);
        if (axis >= 0 && axis < numAxes) serviceByAxis[axis].push_back(t);
    }

    int score = 0;
    for (int axis = 0; axis < numAxes; ++axis) {
        for (auto& it : intentByAxis[axis]) {
            bool matched = false;
            for (auto& st : serviceByAxis[axis]) {
                if (it == st) { score += 10; matched = true; break; }
            }
            if (matched) break;
        }
    }
    return score;
}

static const char* FindSearchUrl(const std::string& serviceName) {
    for (auto& su : kServiceUrls) {
        if (serviceName == su.name) return su.searchFmt;
    }
    return nullptr;
}

}  // namespace

// サービスの説明文を構築（Embedding入力用）
static std::string BuildServiceDescription(const WebServiceRecord& ws) {
    std::string desc = ws.serviceName;
    for (auto& t : ws.tags) {
        desc += " ";
        desc += t;
    }
    return desc;
}

LocalActionResult TryLocalAction(const std::string& speech, LongTermMemory* memory,
                                 SentenceEmbedding* embedding) {
    LocalActionResult result;
    if (!memory || speech.empty()) return result;

    // 1. 発話に含まれる具体的なサービス名を検索
    std::string explicitService;
    for (auto& ws : memory->GetWebServiceRecords()) {
        if (speech.find(ws.serviceName) != std::string::npos) {
            explicitService = ws.serviceName;
            break;
        }
    }

    // 2. スコアリング: Embedding があればコサイン類似度、なければタグ文字列マッチ
    struct Candidate {
        std::string name;
        float score = 0.0f;
        int usageScore = 0;
    };
    std::vector<Candidate> candidates;

    if (embedding && embedding->IsReady()) {
        // --- Embedding モード ---
        auto speechVec = embedding->Encode(speech);
        if (!speechVec.empty()) {
            for (auto& ws : memory->GetWebServiceRecords()) {
                std::string desc = BuildServiceDescription(ws);
                auto svcVec = embedding->Encode(desc);
                if (svcVec.empty()) continue;

                float sim = SentenceEmbedding::CosineSimilarity(speechVec, svcVec);
                if (ws.serviceName == explicitService) sim += 1.0f;

                // 閾値: 0.3 以上でマッチ候補
                if (sim > 0.3f || ws.serviceName == explicitService) {
                    candidates.push_back({ws.serviceName, sim, ws.visitCount});
                }
            }
        }
    }

    if (candidates.empty()) {
        // --- タグ文字列マッチ フォールバック ---
        auto intentTags = ExtractIntentTags(speech);
        if (explicitService.empty() && intentTags.empty()) return result;

        for (auto& ws : memory->GetWebServiceRecords()) {
            int tagScore = ScoreByAxis(intentTags, ws.tags);
            if (ws.serviceName == explicitService) tagScore += 100;
            if (tagScore > 0) {
                candidates.push_back({ws.serviceName, static_cast<float>(tagScore), ws.visitCount});
            }
        }

        if (candidates.empty() && !intentTags.empty()) {
            candidates.push_back({"Google", 1.0f, 0});
        }
    }

    if (candidates.empty()) return result;

    std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.usageScore > b.usageScore;
    });

    const std::string& chosenService = candidates[0].name;

    // 3. URL テンプレートを探す
    const char* urlFmt = FindSearchUrl(chosenService);
    if (!urlFmt) urlFmt = FindSearchUrl("Google");
    if (!urlFmt) return result;

    // 4. 検索クエリ構築
    std::string query;
    auto intentTags = ExtractIntentTags(speech);

    auto isValidQuery = [](const std::string& q) {
        if (q.empty() || q.size() < 2) return false;
        if (q.find_first_not_of("` ,\t\r\n") == std::string::npos) return false;
        return true;
    };

    // 発話から関連する興味カテゴリを特定
    std::vector<std::string> matchedCategories;
    auto addCategory = [&](const std::string& cat) {
        for (auto& m : matchedCategories) if (m == cat) return;
        matchedCategories.push_back(cat);
    };
    for (auto& cat : g_tagAxes.interestCategories) {
        if (speech.find(cat) != std::string::npos) addCategory(cat);
    }
    for (auto& [word, tag] : g_tagAxes.synonyms) {
        if (speech.find(word) != std::string::npos) {
            for (auto& cat : g_tagAxes.interestCategories) {
                if (tag == cat) { addCategory(cat); break; }
            }
        }
    }

    // カテゴリが特定できた場合、そのカテゴリの interests からクエリ構築
    if (!matchedCategories.empty()) {
        for (auto& cat : matchedCategories) {
            auto catInterests = memory->GetInterests(cat, 3);
            for (auto& kw : catInterests) {
                if (!isValidQuery(kw)) continue;
                if (!query.empty()) query += " ";
                query += kw;
            }
        }
    }

    // カテゴリ interests が空 → facts からフィルタ付き検索
    if (query.empty()) {
        auto facts = memory->SearchFacts(chosenService, 10);
        for (auto& f : facts) {
            if (!matchedCategories.empty()) {
                bool relevant = false;
                for (auto& cat : matchedCategories) {
                    if (f.category.find(cat) != std::string::npos ||
                        f.content.find(cat) != std::string::npos) {
                        relevant = true;
                        break;
                    }
                }
                if (!relevant) continue;
            }
            size_t colon = f.content.find(": ");
            if (colon == std::string::npos) colon = f.content.find(":");
            if (colon != std::string::npos && colon + 2 < f.content.size()) {
                std::string candidate = f.content.substr(colon + 2);
                if (isValidQuery(candidate)) {
                    query = candidate;
                    break;
                }
            }
        }
    }

    // 意図タグ名がそのまま interest カテゴリと一致するケース（フォールバック）
    if (query.empty()) {
        for (auto& t : intentTags) {
            auto catInterests = memory->GetInterests(t, 3);
            for (auto& kw : catInterests) {
                if (!isValidQuery(kw)) continue;
                if (!query.empty()) query += " ";
                query += kw;
            }
            if (!query.empty()) break;
        }
    }

    bool isMusicIntent = false;
    for (auto& cat : matchedCategories) {
        if (cat == "\xE9\x9F\xB3\xE6\xA5\xBD") { isMusicIntent = true; break; }
    }
    if (!isMusicIntent) {
        for (auto& t : intentTags) {
            if (t == "\xE9\x9F\xB3\xE6\xA5\xBD") { isMusicIntent = true; break; }
        }
    }
    if (isMusicIntent && !query.empty()) query += " MIX";

    if (query.empty()) query = "trending";

    // 5. URL構築 & 開く
    std::string url = FormatUrl(urlFmt, query);

    std::wstring browserExe;
    const WebServiceRecord* rec = memory->FindWebService(chosenService);
    if (rec && !rec->browserProcess.empty()) {
        browserExe = FindBrowserExe(rec->browserProcess);
    }
    OpenUrl(url, browserExe);

    // 検索結果ページの最初のコンテンツを自動クリック (ページ読み込み待ち4秒)
    AutoClickFirstResult(4000);

    result.handled = true;
    result.url = url;
    result.serviceName = chosenService;
    result.query = query;
    result.spokenText = chosenService;
    result.spokenText += "\xE3\x82\x92\xE9\x96\x8B\xE3\x81\x8F\xE3\x81\xAD"; // を開くね
    return result;
}

bool ContainsActionKeyword(const std::string& text) {
    static const char* kw[] = {
        "\xE8\xAA\xBF\xE3\x81\xB9",     // 調べ
        "\xE6\xA4\x9C\xE7\xB4\xA2",     // 検索
        "\xE6\xB5\x81\xE3\x81\x97\xE3\x81\xA6", // 流して
        "\xE9\x96\x8B\xE3\x81\x84\xE3\x81\xA6", // 開いて
        "\xE8\xA6\x8B\xE3\x81\x9B\xE3\x81\xA6", // 見せて
        "\xE6\x8E\xA2\xE3\x81\x97\xE3\x81\xA6", // 探して
        "\xE3\x81\x8B\xE3\x81\x91\xE3\x81\xA6", // かけて
        "\xE5\x86\x8D\xE7\x94\x9F",     // 再生
        "\xE8\x81\x9E\xE3\x81\x8B\xE3\x81\x9B\xE3\x81\xA6", // 聞かせて
        "\xE3\x81\xA4\xE3\x81\x91\xE3\x81\xA6", // つけて
        "\xE9\x9F\xB3\xE6\xA5\xBD",     // 音楽
        "\xE5\x8B\x95\xE7\x94\xBB",     // 動画
        "YouTube", "youtube",
        "Spotify", "spotify",
    };
    for (const auto* w : kw) {
        if (text.find(w) != std::string::npos) return true;
    }
    return false;
}

const char* GetWebActionPrompt() {
    return
        // ## ブラウザアクション
        "## \xE3\x83\x96\xE3\x83\xA9\xE3\x82\xA6\xE3\x82\xB6\xE3\x82\xA2\xE3\x82\xAF\xE3\x82\xB7\xE3\x83\xA7\xE3\x83\xB3\n"
        // ユーザーの要求に応じて [browser:URL] タグでブラウザを開いてください。
        "\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE3\x81\xAE\xE8\xA6\x81\xE6\xB1\x82\xE3\x81\xAB\xE5\xBF\x9C\xE3\x81\x98\xE3\x81\xA6"
        " [browser:URL] \xE3\x82\xBF\xE3\x82\xB0\xE3\x81\xA7\xE3\x83\x96\xE3\x83\xA9\xE3\x82\xA6\xE3\x82\xB6\xE3\x82\x92"
        "\xE9\x96\x8B\xE3\x81\x84\xE3\x81\xA6\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84\xE3\x80\x82\n\n"
        // ### 重要：行動で応える
        "### \xE9\x87\x8D\xE8\xA6\x81\xEF\xBC\x9A\xE8\xA1\x8C\xE5\x8B\x95\xE3\x81\xA7\xE5\xBF\x9C\xE3\x81\x88\xE3\x82\x8B\n"
        // キーワードの提案や選択肢の提示ではなく、必ず [browser:URL] で実際に開くこと。
        "\xE3\x82\xAD\xE3\x83\xBC\xE3\x83\xAF\xE3\x83\xBC\xE3\x83\x89\xE3\x81\xAE\xE6\x8F\x90\xE6\xA1\x88\xE3\x82\x84"
        "\xE9\x81\xB8\xE6\x8A\x9E\xE8\x82\xA2\xE3\x81\xAE\xE6\x8F\x90\xE7\xA4\xBA\xE3\x81\xA7\xE3\x81\xAF\xE3\x81\xAA\xE3\x81\x8F\xE3\x80\x81"
        "\xE5\xBF\x85\xE3\x81\x9A [browser:URL] \xE3\x81\xA7\xE5\xAE\x9F\xE9\x9A\x9B\xE3\x81\xAB\xE9\x96\x8B\xE3\x81\x8F\xE3\x81\x93\xE3\x81\xA8\xE3\x80\x82\n\n"
        // ### 空気を読む
        "### \xE7\xA9\xBA\xE6\xB0\x97\xE3\x82\x92\xE8\xAA\xAD\xE3\x82\x80\n"
        // 曖昧な指示でも、ユーザー情報から推測して即座に行動すること：
        "\xE6\x9B\x96\xE6\x98\xA7\xE3\x81\xAA\xE6\x8C\x87\xE7\xA4\xBA\xE3\x81\xA7\xE3\x82\x82\xE3\x80\x81"
        "\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE6\x83\x85\xE5\xA0\xB1\xE3\x81\x8B\xE3\x82\x89\xE6\x8E\xA8\xE6\xB8\xAC\xE3\x81\x97\xE3\x81\xA6"
        "\xE5\x8D\xB3\xE5\xBA\xA7\xE3\x81\xAB\xE8\xA1\x8C\xE5\x8B\x95\xE3\x81\x99\xE3\x82\x8B\xE3\x81\x93\xE3\x81\xA8\xEF\xBC\x9A\n"
        // - 「音楽かけて」→「よく使うWebサービス」で音楽系の利用が多いサービスを選び、「興味」や「記憶した事実」からユーザーの好みに合う検索クエリを構築して開く
        "- \xE3\x80\x8C\xE9\x9F\xB3\xE6\xA5\xBD\xE3\x81\x8B\xE3\x81\x91\xE3\x81\xA6\xE3\x80\x8D\xE2\x86\x92"
        "\xE3\x80\x8C\xE3\x82\x88\xE3\x81\x8F\xE4\xBD\xBF\xE3\x81\x86Web\xE3\x82\xB5\xE3\x83\xBC\xE3\x83\x93\xE3\x82\xB9\xE3\x80\x8D\xE3\x81\xA7"
        "\xE9\x9F\xB3\xE6\xA5\xBD\xE7\xB3\xBB\xE3\x81\xAE\xE5\x88\xA9\xE7\x94\xA8\xE3\x81\x8C\xE5\xA4\x9A\xE3\x81\x84"
        "\xE3\x82\xB5\xE3\x83\xBC\xE3\x83\x93\xE3\x82\xB9\xE3\x82\x92\xE9\x81\xB8\xE3\x81\xB3\xE3\x80\x81"
        "\xE3\x80\x8C\xE8\x88\x88\xE5\x91\xB3\xE3\x80\x8D\xE3\x82\x84\xE3\x80\x8C\xE8\xA8\x98\xE6\x86\xB6\xE3\x81\x97\xE3\x81\x9F\xE4\xBA\x8B\xE5\xAE\x9F\xE3\x80\x8D\xE3\x81\x8B\xE3\x82\x89"
        "\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE3\x81\xAE\xE5\xA5\xBD\xE3\x81\xBF\xE3\x81\xAB\xE5\x90\x88\xE3\x81\x86"
        "\xE6\xA4\x9C\xE7\xB4\xA2\xE3\x82\xAF\xE3\x82\xA8\xE3\x83\xAA\xE3\x82\x92\xE6\xA7\x8B\xE7\xAF\x89\xE3\x81\x97\xE3\x81\xA6\xE9\x96\x8B\xE3\x81\x8F\n"
        // - 「なんか面白いの見せて」→ ユーザーの興味に合った動画や記事を検索して開く
        "- \xE3\x80\x8C\xE3\x81\xAA\xE3\x82\x93\xE3\x81\x8B\xE9\x9D\xA2\xE7\x99\xBD\xE3\x81\x84\xE3\x81\xAE\xE8\xA6\x8B\xE3\x81\x9B\xE3\x81\xA6\xE3\x80\x8D\xE2\x86\x92"
        "\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE3\x81\xAE\xE8\x88\x88\xE5\x91\xB3\xE3\x81\xAB\xE5\x90\x88\xE3\x81\xA3\xE3\x81\x9F"
        "\xE5\x8B\x95\xE7\x94\xBB\xE3\x82\x84\xE8\xA8\x98\xE4\xBA\x8B\xE3\x82\x92\xE6\xA4\x9C\xE7\xB4\xA2\xE3\x81\x97\xE3\x81\xA6\xE9\x96\x8B\xE3\x81\x8F\n"
        // - サービスが明示されていなければ、ユーザーの利用データから最適なサービスを選ぶ
        "- \xE3\x82\xB5\xE3\x83\xBC\xE3\x83\x93\xE3\x82\xB9\xE3\x81\x8C\xE6\x98\x8E\xE7\xA4\xBA\xE3\x81\x95\xE3\x82\x8C\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xAA\xE3\x81\x91\xE3\x82\x8C\xE3\x81\xB0\xE3\x80\x81"
        "\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE3\x81\xAE\xE5\x88\xA9\xE7\x94\xA8\xE3\x83\x87\xE3\x83\xBC\xE3\x82\xBF\xE3\x81\x8B\xE3\x82\x89"
        "\xE6\x9C\x80\xE9\x81\xA9\xE3\x81\xAA\xE3\x82\xB5\xE3\x83\xBC\xE3\x83\x93\xE3\x82\xB9\xE3\x82\x92\xE9\x81\xB8\xE3\x81\xB6\n"
        // - 具体的な曲名やアーティスト名がなければ、ユーザーの興味関心から推測する
        "- \xE5\x85\xB7\xE4\xBD\x93\xE7\x9A\x84\xE3\x81\xAA\xE6\x9B\xB2\xE5\x90\x8D\xE3\x82\x84\xE3\x82\xA2\xE3\x83\xBC\xE3\x83\x86\xE3\x82\xA3\xE3\x82\xB9\xE3\x83\x88\xE5\x90\x8D\xE3\x81\x8C\xE3\x81\xAA\xE3\x81\x91\xE3\x82\x8C\xE3\x81\xB0\xE3\x80\x81"
        "\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE3\x81\xAE\xE8\x88\x88\xE5\x91\xB3\xE9\x96\xA2\xE5\xBF\x83\xE3\x81\x8B\xE3\x82\x89\xE6\x8E\xA8\xE6\xB8\xAC\xE3\x81\x99\xE3\x82\x8B\n\n"
        // ### 形式
        "### \xE5\xBD\xA2\xE5\xBC\x8F\n"
        "- [browser:https://www.youtube.com/results?search_query=query]\n"
        "- [browser:https://open.spotify.com/search/query]\n"
        "- [browser:https://www.google.com/search?q=query]\n"
        "- [browser:https://x.com/search?q=query]\n\n"
        // URLのクエリパラメータはURLエンコードしてください。
        "URL\xE3\x81\xAE\xE3\x82\xAF\xE3\x82\xA8\xE3\x83\xAA\xE3\x83\x91\xE3\x83\xA9\xE3\x83\xA1\xE3\x83\xBC\xE3\x82\xBF"
        "\xE3\x81\xAFURL\xE3\x82\xA8\xE3\x83\xB3\xE3\x82\xB3\xE3\x83\xBC\xE3\x83\x89\xE3\x81\x97\xE3\x81\xA6"
        "\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84\xE3\x80\x82\n"
        // タグの前後に短い会話テキストを添えてください。
        "\xE3\x82\xBF\xE3\x82\xB0\xE3\x81\xAE\xE5\x89\x8D\xE5\xBE\x8C\xE3\x81\xAB\xE7\x9F\xAD\xE3\x81\x84"
        "\xE4\xBC\x9A\xE8\xA9\xB1\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88\xE3\x82\x92\xE6\xB7\xBB\xE3\x81\x88"
        "\xE3\x81\xA6\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84\xE3\x80\x82\n"
        // 要約が求められた場合はブラウザを開かず、テキストで回答してください。
        "\xE8\xA6\x81\xE7\xB4\x84\xE3\x81\x8C\xE6\xB1\x82\xE3\x82\x81\xE3\x82\x89\xE3\x82\x8C\xE3\x81\x9F\xE5\xA0\xB4"
        "\xE5\x90\x88\xE3\x81\xAF\xE3\x83\x96\xE3\x83\xA9\xE3\x82\xA6\xE3\x82\xB6\xE3\x82\x92\xE9\x96\x8B\xE3\x81\x8B"
        "\xE3\x81\x9A\xE3\x80\x81\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88\xE3\x81\xA7\xE5\x9B\x9E\xE7\xAD\x94"
        "\xE3\x81\x97\xE3\x81\xA6\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84\xE3\x80\x82\n";
}

bool LoadTagAxes(const std::string& jsonPath) {
    std::ifstream ifs(jsonPath, std::ios::binary);
    if (!ifs) return false;
    std::string json((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    g_tagAxes = TagAxesData{};

    // "axes" オブジェクトをパース
    auto axesPos = json.find("\"axes\"");
    if (axesPos == std::string::npos) return false;
    auto axesBrace = json.find('{', axesPos + 6);
    if (axesBrace == std::string::npos) return false;

    // axes 内の各キー（軸名）と配列を抽出
    size_t cur = axesBrace + 1;
    int axisIndex = 0;
    while (cur < json.size()) {
        auto keyStart = json.find('"', cur);
        if (keyStart == std::string::npos) break;
        auto keyEnd = json.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;

        // axes の閉じ括弧を超えたら終了
        auto closeBrace = json.find('}', cur);
        if (closeBrace != std::string::npos && closeBrace < keyStart) break;

        std::string axisName = json.substr(keyStart + 1, keyEnd - keyStart - 1);

        auto arrStart = json.find('[', keyEnd);
        if (arrStart == std::string::npos) break;
        auto arrEnd = json.find(']', arrStart);
        if (arrEnd == std::string::npos) break;

        std::string arrStr = json.substr(arrStart + 1, arrEnd - arrStart - 1);
        auto tags = ParseJsonStringArray(arrStr);

        g_tagAxes.axisNames.push_back(axisName);
        g_tagAxes.axisTagLists.push_back(tags);
        for (auto& t : tags) {
            g_tagAxes.tagToAxis[t] = axisIndex;
            g_tagAxes.allTags.push_back(t);
        }
        ++axisIndex;
        cur = arrEnd + 1;
    }

    // "synonyms" オブジェクトをパース
    auto synPos = json.find("\"synonyms\"");
    if (synPos != std::string::npos) {
        auto synBrace = json.find('{', synPos + 10);
        if (synBrace != std::string::npos) {
            auto synClose = json.find('}', synBrace);
            if (synClose != std::string::npos) {
                size_t sc = synBrace + 1;
                while (sc < synClose) {
                    auto k1 = json.find('"', sc);
                    if (k1 == std::string::npos || k1 >= synClose) break;
                    auto k2 = json.find('"', k1 + 1);
                    if (k2 == std::string::npos) break;
                    auto v1 = json.find('"', k2 + 1);
                    if (v1 == std::string::npos) break;
                    auto v2 = json.find('"', v1 + 1);
                    if (v2 == std::string::npos) break;

                    std::string word = json.substr(k1 + 1, k2 - k1 - 1);
                    std::string tag = json.substr(v1 + 1, v2 - v1 - 1);
                    g_tagAxes.synonyms[word] = tag;
                    sc = v2 + 1;
                }
            }
        }
    }

    // "interest_categories" 配列をパース
    auto icPos = json.find("\"interest_categories\"");
    if (icPos != std::string::npos) {
        auto icArr = json.find('[', icPos);
        if (icArr != std::string::npos) {
            auto icEnd = json.find(']', icArr);
            if (icEnd != std::string::npos) {
                std::string arrStr = json.substr(icArr + 1, icEnd - icArr - 1);
                g_tagAxes.interestCategories = ParseJsonStringArray(arrStr);
            }
        }
    }

    g_tagAxes.loaded = true;
    return !g_tagAxes.axisNames.empty();
}

void LearnInterestsFromSpeech(const std::string& speech, LongTermMemory* memory) {
    if (!memory || speech.empty() || !g_tagAxes.loaded) return;

    auto intentTags = ExtractIntentTags(speech);
    if (intentTags.empty()) return;

    // 発話から既知のワードを除去して主語（キーワード）を抽出
    std::string subject = speech;

    // タグ語彙を除去
    for (auto& tag : g_tagAxes.allTags) {
        size_t pos;
        while ((pos = subject.find(tag)) != std::string::npos) {
            subject.erase(pos, tag.size());
        }
    }
    // シノニムを除去
    for (auto& [word, _] : g_tagAxes.synonyms) {
        size_t pos;
        while ((pos = subject.find(word)) != std::string::npos) {
            subject.erase(pos, word.size());
        }
    }

    // アクションキーワードを除去
    static const char* kStopWords[] = {
        "\xe8\xaa\xbf\xe3\x81\xb9",         // 調べ
        "\xe6\xa4\x9c\xe7\xb4\xa2",         // 検索
        "\xe6\xb5\x81\xe3\x81\x97\xe3\x81\xa6", // 流して
        "\xe9\x96\x8b\xe3\x81\x84\xe3\x81\xa6", // 開いて
        "\xe8\xa6\x8b\xe3\x81\x9b\xe3\x81\xa6", // 見せて
        "\xe6\x8e\xa2\xe3\x81\x97\xe3\x81\xa6", // 探して
        "\xe3\x81\x8b\xe3\x81\x91\xe3\x81\xa6", // かけて
        "\xe5\x86\x8d\xe7\x94\x9f",         // 再生
        "\xe8\x81\x9e\xe3\x81\x8b\xe3\x81\x9b\xe3\x81\xa6", // 聞かせて
        "\xe3\x81\xa4\xe3\x81\x91\xe3\x81\xa6", // つけて
        "\xe8\x81\xb4\xe3\x81\x8d\xe3\x81\x9f\xe3\x81\x84", // 聴きたい
        "\xe8\xa6\x8b\xe3\x81\x9f\xe3\x81\x84", // 見たい
        "\xe3\x81\x97\xe3\x81\x9f\xe3\x81\x84", // したい
        "\xe3\x81\xbb\xe3\x81\x97\xe3\x81\x84", // ほしい
        "\xe3\x81\xae",     // の
        "\xe3\x82\x92",     // を
        "\xe3\x81\xab",     // に
        "\xe3\x81\x8c",     // が
        "\xe3\x81\xa7",     // で
        "\xe3\x81\xaf",     // は
        "\xe3\x81\xa8",     // と
        "\xe3\x82\x82",     // も
    };
    for (auto* w : kStopWords) {
        size_t pos;
        while ((pos = subject.find(w)) != std::string::npos) {
            subject.erase(pos, strlen(w));
        }
    }

    // 空白・記号をトリム
    while (!subject.empty() && (subject.front() == ' ' || subject.front() == '\t')) {
        subject.erase(0, 1);
    }
    while (!subject.empty() && (subject.back() == ' ' || subject.back() == '\t' ||
           subject.back() == '?' || subject.back() == '\xef')) {
        subject.pop_back();
    }

    if (subject.empty() || subject.size() < 2) return;

    // 最初にマッチしたタグのカテゴリに追加
    for (auto& tag : intentTags) {
        int axis = GetTagAxis(tag);
        if (axis >= 0 && axis < static_cast<int>(g_tagAxes.axisNames.size())) {
            memory->AddInterest(tag, subject);
            return;
        }
    }
}
