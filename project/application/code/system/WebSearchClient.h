#pragma once

#include <string>
#include <vector>

/// <summary>Web 検索の 1 件分の結果。</summary>
struct WebResult {
    std::string title;
    std::string snippet;
    std::string url;
};

/// <summary>
/// 時事対策（鮮度補強）。DuckDuckGo の HTML エンドポイント（API キー不要）を叩いて
/// 検索結果を取得し、プロンプトに注入できる形へ整形する。
/// 「最新」「今日」「ニュース」等の時事キーワードでクエリを自動検出する。
/// ※ Search/BuildContext は同期（ブロッキング）。メインスレッドでは呼ばず
///   バックグラウンドスレッドから使うこと（curl による HTTP 取得のため）。
/// </summary>
class WebSearchClient {
public:
    // 時事キーワードを JSON（{"keywords":[...]} ）から読み込む。無ければ既定値を使う。
    bool LoadKeywords(const std::string& jsonPath);
    // クエリが時事性（鮮度が要る情報）を含むか。
    bool NeedsFreshInfo(const std::string& text) const;

    void SetEnabled(bool e) { enabled_ = e; }
    bool IsEnabled() const { return enabled_; }
    void SetTimeoutSec(long s) { timeoutSec_ = s; }

    // DuckDuckGo で検索し上位 maxResults 件を返す（同期）。
    std::vector<WebResult> Search(const std::string& query, int maxResults = 4) const;
    // 検索結果をプロンプト注入用テキスト（"## Web検索結果（最新情報）..."）に整形。空ならヒットなし。
    std::string BuildContext(const std::string& query, int maxResults = 4) const;

    const std::vector<std::string>& Keywords() const { return keywords_; }

    // --- ツール（関数呼び出し）: モデル主導の Web 検索 ---
    // 既存の [browser:URL] と同じタグ方式。モデルが [search: キーワード] を出力したら検索する。
    static const char* ToolPrompt();                              // モデルへの使い方指示（system へ付与）
    static std::string ParseSearchQuery(const std::string& text); // [search:...] を抽出（無ければ ""）
    static std::string StripSearchTags(const std::string& text);  // 出力から [search:...] を除去

    // 文字列ユーティリティ（公開: テスト・再利用用）
    static std::string UrlEncode(const std::string& s);
    static std::string HtmlDecode(const std::string& s);
    static std::string StripTags(const std::string& s);

private:
    std::string HttpGet(const std::string& url) const;
    static std::string ExtractInner(const std::string& html, size_t openTagEnd, const std::string& closeTag);
    static std::string DecodeDdgHref(const std::string& href);

    std::vector<std::string> keywords_ = {
        // 既定の時事キーワード（current_events.json が無い場合のフォールバック）
        "\xe6\x9c\x80\xe6\x96\xb0",       // 最新
        "\xe4\xbb\x8a\xe6\x97\xa5",       // 今日
        "\xe7\x8f\xbe\xe5\x9c\xa8",       // 現在
        "\xe3\x83\x8b\xe3\x83\xa5\xe3\x83\xbc\xe3\x82\xb9", // ニュース
        "\xe9\x80\x9f\xe5\xa0\xb1",       // 速報
        "\xe7\x9b\xb4\xe8\xbf\x91",       // 直近
        "\xe6\x98\xa8\xe6\x97\xa5",       // 昨日
        "\xe4\xbb\x8a\xe5\xb9\xb4",       // 今年
        "\xe4\xbb\x8a\xe6\x9c\x88",       // 今月
        "\xe3\x83\xaa\xe3\x82\xa2\xe3\x83\xab\xe3\x82\xbf\xe3\x82\xa4\xe3\x83\xa0", // リアルタイム
        "\xe5\xa4\xa9\xe6\xb0\x97",       // 天気
        "\xe7\x82\xba\xe6\x9b\xbf",       // 為替
        "\xe6\xa0\xaa\xe4\xbe\xa1",       // 株価
        "\xe7\x99\xba\xe5\xa3\xb2",       // 発売
    };
    bool enabled_     = true;
    long timeoutSec_  = 6;
};
