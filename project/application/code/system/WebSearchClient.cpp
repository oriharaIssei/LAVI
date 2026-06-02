#include "WebSearchClient.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

size_t WriteCb(void* contents, size_t size, size_t nmemb, std::string* out) {
    const size_t n = size * nmemb;
    out->append(static_cast<char*>(contents), n);
    return n;
}

// "result__a" / "result__snippet" の出現位置から、その開きタグの '>' 位置を返す。
size_t FindTagOpenEnd(const std::string& html, size_t classPos) {
    return html.find('>', classPos);
}

} // namespace

std::string WebSearchClient::UrlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    char buf[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string WebSearchClient::HtmlDecode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            const size_t semi = s.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                const std::string ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp")       { out += '&'; i = semi + 1; continue; }
                if (ent == "lt")        { out += '<'; i = semi + 1; continue; }
                if (ent == "gt")        { out += '>'; i = semi + 1; continue; }
                if (ent == "quot")      { out += '"'; i = semi + 1; continue; }
                if (ent == "apos")      { out += '\''; i = semi + 1; continue; }
                if (ent == "#39" || ent == "#x27") { out += '\''; i = semi + 1; continue; }
                if (ent == "nbsp")      { out += ' '; i = semi + 1; continue; }
                if (!ent.empty() && ent[0] == '#') {
                    // 数値文字参照（簡易: UTF-8 へ）
                    long cp = 0;
                    if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                        cp = std::strtol(ent.c_str() + 2, nullptr, 16);
                    else
                        cp = std::strtol(ent.c_str() + 1, nullptr, 10);
                    if (cp > 0) {
                        if (cp < 0x80) out += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        i = semi + 1;
                        continue;
                    }
                }
            }
        }
        out += s[i++];
    }
    return out;
}

std::string WebSearchClient::StripTags(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool inTag = false;
    for (char c : s) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out += c;
    }
    return out;
}

const char* WebSearchClient::ToolPrompt() {
    return
        "あなたは Web 検索ツールを使えます。天気・ニュース・最新の出来事・時事・株価・為替・"
        "スポーツ結果など、リアルタイム性のある情報や日付に依存する情報、あなたが確信を持てない"
        "事実が必要なときは、絶対に「アクセスできません」「分かりません」「調べられません」などと"
        "答えてはいけません。代わりに、回答を一切書かず [search: 検索キーワード] という形式だけを"
        "1 行で出力してください。\n"
        "例:「今日の天気を教えて」→ [search: 今日の天気]\n"
        "例:「最新のニュースは?」→ [search: 最新ニュース]\n"
        "例:「ドル円いくつ?」→ [search: ドル円 為替]\n"
        "検索が不要な雑談や一般常識の質問には、通常どおり回答してください。"
        "検索結果が与えられたら、その内容を踏まえて自然な口調で回答してください。";
}

std::string WebSearchClient::ParseSearchQuery(const std::string& text) {
    static const std::string key = "[search:";
    const size_t s = text.find(key);
    if (s == std::string::npos) return std::string();
    const size_t qs = s + key.size();
    const size_t qe = text.find(']', qs);
    if (qe == std::string::npos) return std::string();
    std::string q = text.substr(qs, qe - qs);
    const size_t b = q.find_first_not_of(" \t\r\n\xe3\x80\x80"); // 半角/全角空白
    if (b == std::string::npos) return std::string();
    const size_t e = q.find_last_not_of(" \t\r\n\xe3\x80\x80");
    return q.substr(b, e - b + 1);
}

std::string WebSearchClient::StripSearchTags(const std::string& text) {
    static const std::string key = "[search:";
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        const size_t s = text.find(key, i);
        if (s == std::string::npos) { out += text.substr(i); break; }
        out += text.substr(i, s - i);
        const size_t e = text.find(']', s);
        if (e == std::string::npos) break; // 未閉じタグ以降は破棄
        i = e + 1;
    }
    const size_t b = out.find_first_not_of(" \t\r\n");
    return (b == std::string::npos) ? std::string() : out.substr(b);
}

bool WebSearchClient::LoadKeywords(const std::string& jsonPath) {
    std::ifstream f(jsonPath);
    if (!f.is_open()) return false; // 既定値のまま
    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("keywords") && j["keywords"].is_array()) {
            std::vector<std::string> kw;
            for (const auto& e : j["keywords"]) {
                if (e.is_string()) kw.push_back(e.get<std::string>());
            }
            if (!kw.empty()) keywords_ = std::move(kw);
        }
        if (j.contains("enabled") && j["enabled"].is_boolean()) enabled_ = j["enabled"].get<bool>();
    } catch (...) {
        return false;
    }
    return true;
}

bool WebSearchClient::NeedsFreshInfo(const std::string& text) const {
    if (!enabled_ || text.empty()) return false;
    for (const auto& kw : keywords_) {
        if (!kw.empty() && text.find(kw) != std::string::npos) return true;
    }
    return false;
}

std::string WebSearchClient::HttpGet(const std::string& url) const {
    CURL* curl = curl_easy_init();
    if (!curl) return std::string();

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: text/html");
    headers = curl_slist_append(headers, "Accept-Language: ja,en;q=0.8");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // DDG の html エンドポイントは UA が無いと空を返すことがある
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                     "(KHTML, like Gecko) Chrome/124.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    // NOSIGNAL は必須: ワーカースレッドではシグナルベースのタイムアウトが効かず、
    // 接続が詰まると CURLOPT_TIMEOUT を無視して無限待ちになる（メインスレッド外で多発）。
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSec_);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeoutSec_);

    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return std::string();
    return response;
}

std::string WebSearchClient::ExtractInner(const std::string& html, size_t openTagEnd, const std::string& closeTag) {
    if (openTagEnd == std::string::npos) return std::string();
    const size_t start = openTagEnd + 1;
    const size_t end = html.find(closeTag, start);
    if (end == std::string::npos) return std::string();
    return html.substr(start, end - start);
}

std::string WebSearchClient::DecodeDdgHref(const std::string& href) {
    // 形式: //duckduckgo.com/l/?uddg=<URLエンコード済み>&rut=...
    const std::string key = "uddg=";
    const size_t k = href.find(key);
    if (k == std::string::npos) return href;
    size_t s = k + key.size();
    size_t e = href.find('&', s);
    std::string enc = (e == std::string::npos) ? href.substr(s) : href.substr(s, e - s);

    std::string out;
    for (size_t i = 0; i < enc.size(); ++i) {
        if (enc[i] == '%' && i + 2 < enc.size()) {
            const int hi = std::isxdigit(static_cast<unsigned char>(enc[i + 1])) ? std::strtol(enc.substr(i + 1, 2).c_str(), nullptr, 16) : -1;
            if (hi >= 0) { out += static_cast<char>(hi); i += 2; continue; }
        }
        if (enc[i] == '+') out += ' ';
        else out += enc[i];
    }
    return out;
}

std::vector<WebResult> WebSearchClient::Search(const std::string& query, int maxResults) const {
    std::vector<WebResult> results;
    if (!enabled_ || query.empty()) return results;

    const std::string url = "https://html.duckduckgo.com/html/?q=" + UrlEncode(query);
    const std::string html = HttpGet(url);
    if (html.empty()) return results;

    // result__a（タイトル + href）と result__snippet（本文）を順に抽出してペアにする。
    std::vector<std::pair<std::string, std::string>> titles; // (title, url)
    std::vector<std::string> snippets;

    size_t pos = 0;
    while ((pos = html.find("result__a\"", pos)) != std::string::npos) {
        // href を開きタグ内から拾う
        const size_t tagStart = html.rfind('<', pos);
        const size_t tagEnd = FindTagOpenEnd(html, pos);
        std::string href;
        if (tagStart != std::string::npos && tagEnd != std::string::npos) {
            const std::string tag = html.substr(tagStart, tagEnd - tagStart);
            const size_t h = tag.find("href=\"");
            if (h != std::string::npos) {
                const size_t hs = h + 6;
                const size_t he = tag.find('"', hs);
                if (he != std::string::npos) href = DecodeDdgHref(tag.substr(hs, he - hs));
            }
        }
        const std::string inner = ExtractInner(html, tagEnd, "</a>");
        titles.emplace_back(HtmlDecode(StripTags(inner)), href);
        pos = tagEnd == std::string::npos ? pos + 1 : tagEnd + 1;
        if (static_cast<int>(titles.size()) >= maxResults) break;
    }

    pos = 0;
    while ((pos = html.find("result__snippet", pos)) != std::string::npos) {
        const size_t tagEnd = FindTagOpenEnd(html, pos);
        const std::string inner = ExtractInner(html, tagEnd, "</a>");
        snippets.push_back(HtmlDecode(StripTags(inner)));
        pos = tagEnd == std::string::npos ? pos + 1 : tagEnd + 1;
        if (static_cast<int>(snippets.size()) >= maxResults) break;
    }

    for (size_t i = 0; i < titles.size() && static_cast<int>(results.size()) < maxResults; ++i) {
        WebResult r;
        r.title   = titles[i].first;
        r.url     = titles[i].second;
        r.snippet = (i < snippets.size()) ? snippets[i] : std::string();
        if (r.title.empty() && r.snippet.empty()) continue;
        results.push_back(std::move(r));
    }
    return results;
}

std::string WebSearchClient::BuildContext(const std::string& query, int maxResults) const {
    const std::vector<WebResult> hits = Search(query, maxResults);
    if (hits.empty()) return std::string();

    std::ostringstream ctx;
    ctx << "## Web\xe6\xa4\x9c\xe7\xb4\xa2\xe7\xb5\x90\xe6\x9e\x9c\xef\xbc\x88\xe6\x9c\x80\xe6\x96\xb0\xe6\x83\x85\xe5\xa0\xb1\xef\xbc\x89\n"; // ## Web検索結果（最新情報）
    for (const auto& h : hits) {
        ctx << "- ";
        if (!h.title.empty()) ctx << h.title << ": ";
        ctx << h.snippet;
        if (!h.url.empty()) ctx << "\xef\xbc\x88" << h.url << "\xef\xbc\x89"; // （url）
        ctx << "\n";
    }
    return ctx.str();
}
