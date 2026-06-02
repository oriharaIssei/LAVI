#pragma once

#include <string>

// Intent-Driven アクションの共通表現。
// verb（行為）と target（対象＝サービス/アプリ名）を分離し、target は文字列 Entity として扱う。
// enum でサービスを固定しない（拡張・未知サービス対応のため）。

/// <summary>抽出されたエンティティ。type は "service" / "app" など。</summary>
struct Entity {
    std::string type; // "service" | "app"
    std::string name; // "youtube" / "twitter" / "notepad" ...
};

/// <summary>
/// 1 つの行動要求。Planner 前提で、1 発話から複数生成され得る（vector で扱う）。
/// verb: "open" | "search" | "launch"（拡張可能・文字列）
/// target: ツール名（ToolRegistry が動的解決）
/// query: 検索語など（任意）
/// </summary>
struct ActionRequest {
    std::string verb;
    std::string target;
    std::string query;

    bool IsValid() const { return !verb.empty(); }
};
