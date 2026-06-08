#include "ActionSystem.h"

#include "system/action/ActionPipeline.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"

ActionSystem::ActionSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

ActionSystem::~ActionSystem() = default;

void ActionSystem::Initialize() {
    actionPipeline_ = std::make_unique<ActionPipeline>();
    // ツール定義は JSON から読み込む（ToolRegistry）。
    actionPipeline_->LoadConfig("application/resource/action/tools.json");
    LaviContext::Get().actionPipeline = actionPipeline_.get(); // 共有状態に公開（消費側は LaviContext 経由）
}

void ActionSystem::Finalize() {
    LaviContext::Get().actionPipeline = nullptr;
    actionPipeline_.reset();
}
