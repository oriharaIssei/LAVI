#pragma once

#include "system/ISystem.h"

#include "CameraGatekeeper.h" // EmotionResult 完全定義（std::future<EmotionResult> メンバのため）

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>

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

    // 重い Evaluate（Haar+ONNX）をワーカースレッドへ退避（メインスレッドのストール解消）。
    // 同時に 1 評価のみ（evalBusy_）＝CameraGatekeeper をワーカーが排他使用する。
    std::future<EmotionResult> evalFuture_;
    bool evalBusy_            = false;
    uint64_t lastEvaluatedSeq_ = 0; // 同一フレームの二重評価を防ぐ
};
