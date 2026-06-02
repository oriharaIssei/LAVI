#pragma once

#include "ActionTypes.h"
#include "ActionRequest.h"
#include "ToolRegistry.h"
#include "ActionPlanner.h"
#include "ActionExecutor.h"
#include "IntentParser.h"

#include <string>

/// <summary>
/// Intent-Driven なアクション統合層。
/// キーワードマッチではなく、LLM が出力した [action:{verb,target,query}] を解釈し、
/// ToolRegistry で対象を動的解決して Executor が実行する。
///
/// 流れ: LLM出力 → IntentParser → ActionRequest[] → Planner → Executor(ToolRegistry解決)
/// </summary>
class ActionPipeline {
public:
    bool LoadConfig(const std::string& toolsJsonPath); // ToolRegistry をロード

    // LLM へ渡すツール使用指示（system プロンプトに付与）。利用可能ツール一覧を含む。
    std::string BuildToolPrompt() const;

    // LLM 出力中の [action:...] を解釈し、計画（ActionPlan）を組んで実行する。最後の結果を返す。
    ActionResult ParseAndExecute(const std::string& llmOutput);

    // 発話・表示用に [action:...] タグを除去する。
    static std::string StripActionTags(const std::string& text) { return ActionIntent::Strip(text); }

    const ToolRegistry& Registry() const { return registry_; }
    const std::string& LastPlanSummary() const { return lastPlanSummary_; } // 直近に実行した計画の説明

private:
    ToolRegistry  registry_;
    ActionPlanner planner_;
    ActionExecutor executor_;
    std::string   lastPlanSummary_;
};
