#pragma once

#include "system/ISystem.h"

#include <chrono>
#include <memory>

class CameraGatekeeper;

/// <summary>
/// カメラ（表情）キャプチャ評価システム（Category=Input）。
/// CameraGatekeeper を所有し、間隔ごとに表情を評価して、変化を検知したら
/// CapturePromptComponent（source=Camera）を 1 件発行する。GatekeeperSystem がそれを消費する。
/// 設定(enabled/interval)は GatekeeperManager::config() を LaviContext 経由で参照。
/// GK ポインタ・結果・フレーム寸法は LaviContext に公開（GatekeeperPanel/MemoryPanel 用）。
/// </summary>
class CameraGateSystem : public OriGine::ISystem {
public:
    CameraGateSystem();
    ~CameraGateSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<CameraGatekeeper> camera_;
    std::chrono::steady_clock::time_point lastEval_{};
};
