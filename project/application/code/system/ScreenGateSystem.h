#pragma once

#include "system/ISystem.h"

#include <chrono>
#include <memory>

class ScreenGatekeeper;

/// <summary>
/// 画面キャプチャ評価システム（Category=Input）。
/// ScreenGatekeeper を所有し、間隔ごとに画面（プロセス変化/画素差分）を評価して、
/// 変化を検知したら CapturePromptComponent（source=Screen）を発行する。
/// </summary>
class ScreenGateSystem : public OriGine::ISystem {
public:
    ScreenGateSystem();
    ~ScreenGateSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<ScreenGatekeeper> screen_;
    std::chrono::steady_clock::time_point lastEval_{};
};
