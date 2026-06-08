#pragma once

#include "system/ISystem.h"

#include <memory>

class ActionPipeline;

/// <summary>
/// Intent-Driven アクション実行（ActionPipeline）を担う ECS システム。
/// パイプラインを所有し、初期化時に tools.json（ToolRegistry）を読み込んで LaviContext に公開する。
/// 消費側（LLMChatPanel / GatekeeperManager）は LaviContext::Get().actionPipeline を参照する。
///
/// MediaCaptureDemoSystem からの切り出し（ECS 分解）。パターンは KnowledgeSystem と同じ。
/// </summary>
class ActionSystem : public OriGine::ISystem {
public:
    ActionSystem();
    ~ActionSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // アクションは LLM 応答時に実行。毎フレーム処理は不要。

private:
    std::unique_ptr<ActionPipeline> actionPipeline_;
};
