#pragma once

#include "system/ISystem.h"

#include <memory>

class MemoryPanel;

/// <summary>
/// 記憶システム（会話メモリ・アプリ使用追跡・事実抽出・ブラウジング収集・顔識別）を駆動する
/// ECS システム（Category=Movement）。MemoryPanel の機能部（ImGui 非依存の Update）を常時稼働
/// させるための切り出し（ECS 分解）。
/// 所有する MemoryPanel を LaviContext::Get().memoryPanel に公開し、その Initialize 内で
/// LaviContext::Get().longTermMemory / .localLLM も公開される（GatekeeperSystem 等が参照）。
/// 調整 UI（MemoryPanel::Draw）は DEBUG 限定の MediaCaptureDemoSystem が ctx から pull して描画する。
/// Input/StateTransition の後段（Movement）に置き、当フレームの転写・camResult を参照する。
/// </summary>
class MemorySystem : public OriGine::ISystem {
public:
    MemorySystem();
    ~MemorySystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<MemoryPanel> memoryPanel_; // 機能部の所有（旧モノリスから移管）
};
