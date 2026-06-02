#pragma once

#include "ActionRequest.h"

#include <string>
#include <vector>

/// <summary>
/// LLM 出力から行動要求を取り出す。キーワードマッチではなく、自然言語を解釈した LLM が
/// [action:{"verb":"...","target":"...","query":"..."}] 形式で出力したものをパースする。
/// </summary>
namespace ActionIntent {

// [action:{...}] を全て抽出して ActionRequest 列に変換（複数可）。失敗分は無視。
std::vector<ActionRequest> Parse(const std::string& llmOutput);

// 発話・表示用に [action:{...}] タグを除去する。
std::string Strip(const std::string& llmOutput);

} // namespace ActionIntent
