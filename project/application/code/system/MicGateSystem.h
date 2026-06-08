#pragma once

#include "system/ISystem.h"

#include <chrono>
#include <memory>

class MicGatekeeper;

/// <summary>
/// マイク（キーワード）キャプチャ評価システム（Category=Input）。
/// MicGatekeeper を所有し、間隔ごとに転写テキストのキーワードを評価して、
/// 一致を検知したら CapturePromptComponent（source=Mic）を発行する。
/// （発話区間からの自動会話は TurnSystem→GatekeeperManager::RespondToSpeech が別途担当）
/// </summary>
class MicGateSystem : public OriGine::ISystem {
public:
    MicGateSystem();
    ~MicGateSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<MicGatekeeper> mic_;
    std::chrono::steady_clock::time_point lastEval_{};
};
