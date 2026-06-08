#include "MemorySystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "MemoryPanel.h"

MemorySystem::MemorySystem()
    : OriGine::ISystem(OriGine::SystemCategory::Movement) {} // 記憶の更新（行動の結果の蓄積）

MemorySystem::~MemorySystem() = default;

void MemorySystem::Initialize() {
    SharedMediaContext& ctx = LaviContext::Get();
    memoryPanel_ = std::make_unique<MemoryPanel>();
    memoryPanel_->Initialize(&ctx); // 内部で ctx.longTermMemory / .localLLM を公開
    ctx.memoryPanel = memoryPanel_.get(); // DEBUG UI が参照（非所有ポインタ公開）
}

void MemorySystem::Update() {
    // 会話メモリ・アプリ追跡・事実抽出・ブラウジング収集・顔識別の駆動。タブ非表示でも毎フレーム必須。
    memoryPanel_->Update();
}

void MemorySystem::Finalize() {
    LaviContext::Get().memoryPanel = nullptr; // 解放前に公開ポインタを無効化
    memoryPanel_->Finalize();
    memoryPanel_.reset();
}
