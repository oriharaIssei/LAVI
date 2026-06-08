#include "VisionSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "VisionAnalyzer.h"

VisionSystem::VisionSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

VisionSystem::~VisionSystem() = default;

void VisionSystem::Initialize() {
    visionAnalyzer_ = std::make_unique<VisionAnalyzer>();

    // config 由来の API キー / プロンプトを適用（ハードコード回避）。
    // config は System 初期化より前に LaviGame/LaviEditor::Initialize でロード済み。
    SharedMediaContext& ctx = LaviContext::Get();
    if (!ctx.config.apiKey.empty()) {
        visionAnalyzer_->SetApiKey(ctx.config.apiKey);
    }
    if (!ctx.config.visionPrompt.empty()) {
        visionAnalyzer_->SetPrompt(ctx.config.visionPrompt);
    }

    ctx.visionAnalyzer = visionAnalyzer_.get(); // 共有公開（消費側は遅延 pull）
}

void VisionSystem::Finalize() {
    LaviContext::Get().visionAnalyzer = nullptr; // 解放前に公開を無効化
    visionAnalyzer_.reset();
}
