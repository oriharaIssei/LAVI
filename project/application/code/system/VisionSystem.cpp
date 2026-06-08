#include "VisionSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "VisionAnalyzer.h"
#include "LocalVisionAnalyzer.h"
#include "GatekeeperConfig.h"
#include "GatekeeperManager.h"

VisionSystem::VisionSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

VisionSystem::~VisionSystem() = default;

void VisionSystem::Initialize() {
    SharedMediaContext& ctx = LaviContext::Get();

    // --- クラウド（Claude Vision）---
    visionAnalyzer_ = std::make_unique<VisionAnalyzer>();
    if (!ctx.config.apiKey.empty()) {
        visionAnalyzer_->SetApiKey(ctx.config.apiKey);
    }
    if (!ctx.config.visionPrompt.empty()) {
        visionAnalyzer_->SetPrompt(ctx.config.visionPrompt);
    }
    ctx.visionAnalyzer = visionAnalyzer_.get();

    // --- ローカル（Qwen2.5-VL）: パスだけ設定し、モデルロードは遅延（Update）---
    const GatekeeperConfigData gkc = LoadGatekeeperConfig();
    localVision_ = std::make_unique<LocalVisionAnalyzer>();
    localVision_->SetModelPaths(gkc.visionLocalModelPath, gkc.visionLocalMmprojPath);
    if (!ctx.config.visionPrompt.empty()) {
        localVision_->SetPrompt(ctx.config.visionPrompt);
    }
    ctx.localVisionAnalyzer = localVision_.get();
}

void VisionSystem::Finalize() {
    SharedMediaContext& ctx = LaviContext::Get();
    ctx.visionAnalyzer = nullptr;
    ctx.localVisionAnalyzer = nullptr;
    if (localVision_) {
        localVision_->Cancel();
    }
    if (loadFuture_.valid()) {
        loadFuture_.wait();
    }
    localVision_.reset();
    visionAnalyzer_.reset();
}

void VisionSystem::Update() {
    // vision.useLocal が ON になったら一度だけバックグラウンドでローカル VLM をロードする
    // （VRAM 節約のため未使用時はロードしない）。
    if (loadStarted_ || !localVision_) {
        return;
    }
    GatekeeperManager* gk = LaviContext::Get().gkManager;
    const bool wantLocal = gk ? gk->config().visionUseLocal : false;
    if (wantLocal && !localVision_->IsModelLoaded()) {
        loadStarted_ = true;
        LocalVisionAnalyzer* lv = localVision_.get();
        loadFuture_ = std::async(std::launch::async, [lv]() { return lv->LoadModel(); });
    }
}
