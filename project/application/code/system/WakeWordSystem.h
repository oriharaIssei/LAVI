#pragma once

#include "system/ISystem.h"

#include <string>

/// <summary>
/// ウェイクワード検出を担う ECS システム。
/// 毎フレーム ctx の転写テキスト(transcribedText)を監視し、新規部分にウェイクワードが
/// 含まれていればトレイ/最小化からウィンドウを復帰させる。
///
/// 共有状態は LaviContext::Get()（transcribedText / wakeWord / wakeWordEnabled）から参照。
/// MediaCaptureDemoSystem::Update からの切り出し（ECS 分解）。完全独立（micPanel/gkManager に非依存）。
/// </summary>
class WakeWordSystem : public OriGine::ISystem {
public:
    WakeWordSystem();
    ~WakeWordSystem() override;

    void Initialize() override {} // 初期化不要（状態はメンバ既定値）
    void Finalize() override {}   // 終了処理不要

protected:
    void Update() override;

private:
    std::string lastWakeWordText_; // 既処理の転写テキスト（差分検出用）
};
