#pragma once

#include "system/ISystem.h"

#include <future>
#include <memory>

class MicrophonePanel;

/// <summary>
/// マイク入力・音声認識（転写/校正/集約）パイプラインを駆動する ECS システム（Category=Input）。
/// MicrophonePanel の機能部（ImGui 非依存の Update）を常時稼働させるための切り出し（ECS 分解）。
/// 所有する MicrophonePanel を LaviContext::Get().micPanel に公開し、TurnSystem 等が参照する。
/// 調整 UI（MicrophonePanel::Draw）は DEBUG 限定の MediaCaptureDemoSystem が ctx から pull して描画する。
/// </summary>
class MicrophoneSystem : public OriGine::ISystem {
public:
    MicrophoneSystem();
    ~MicrophoneSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<MicrophonePanel> micPanel_; // 機能部の所有（旧モノリスから移管）
    std::future<bool> modelLoadFuture_;         // Whisper モデルの非同期ロード（起動をブロックしない）
};
