#pragma once

#include <string>
#include <vector>

/// <summary>
/// 実行可能なツール（Web サービス / デスクトップアプリ）の定義。
/// enum でサービスを固定せず、JSON から動的に登録する。
/// </summary>
struct Tool {
    std::string name;              // 正規名（小文字推奨）: "youtube" / "notepad"
    std::string type;              // "web" | "app"
    std::string openUrl;           // web: ホームページ URL
    std::string searchUrlTemplate; // web: "{query}" を含む検索 URL テンプレート
    std::string exe;               // app: 実行ファイル名（notepad.exe 等）
    std::vector<std::string> aliases; // 別名（"x","ツイッター" 等）

    bool IsWeb() const { return type == "web"; }
    bool IsApp() const { return type == "app"; }
};

/// <summary>
/// ツールのレジストリ。JSON からロードし、target 名（正規名/別名・大小無視）で動的解決する。
/// Action と Service を分離する中核。
/// </summary>
class ToolRegistry {
public:
    bool Load(const std::string& jsonPath); // application/resource/action/tools.json
    void Register(const Tool& tool);
    void Clear() { tools_.clear(); }

    const Tool* Find(const std::string& target) const; // 正規名→別名の順で解決（大小無視）
    const std::vector<Tool>& All() const { return tools_; }
    bool Empty() const { return tools_.empty(); }

    // LLM へ渡すツール一覧（名前と種別）。プロンプト生成用。
    std::string BuildToolListForPrompt() const;

    // 検索 URL を組み立てる（テンプレートの {query} を URL エンコードして置換）。
    static std::string BuildSearchUrl(const Tool& tool, const std::string& query);
    static std::string UrlEncode(const std::string& s);

private:
    static std::string ToLower(const std::string& s);
    std::vector<Tool> tools_;
};
