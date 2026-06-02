#pragma once

#include "ActionRequest.h"
#include "ToolRegistry.h"

#include <string>
#include <vector>

/// <summary>実行計画の 1 ステップ。Build 後 tool は非 null（解決済み）。</summary>
struct ActionStep {
    ActionRequest request;       // 正規化・フォールバック解決済みの要求
    const Tool*   tool = nullptr; // 実行対象ツール（ToolRegistry 内の実体を指す）
};

/// <summary>順序付き実行計画。複数ステップ（例: 検索→起動）を保持する。</summary>
struct ActionPlan {
    std::vector<ActionStep> steps;
    bool Empty() const { return steps.empty(); }
    // ログ/発話用の説明（例:「YouTubeで『DirectX12』を検索、メモ帳を起動」）。
    std::string Describe() const;
};

/// <summary>
/// ActionRequest 列を実行計画（ActionPlan）に変換する。
/// ツール解決（ToolRegistry）・verb 正規化・連続重複の排除・ステップ数上限・
/// 未知ツールの Google 検索フォールバックをここで一元的に行う（Executor は実行に専念）。
/// 将来の多段タスク（依存・GUI 操作・待機）の拡張点。
/// </summary>
class ActionPlanner {
public:
    ActionPlan Build(const std::vector<ActionRequest>& requests, const ToolRegistry& registry) const;

    int maxSteps = 4; // 1 発話あたりの実行ステップ上限（暴発防止）
};
