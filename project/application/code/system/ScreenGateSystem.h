#pragma once

#include "system/ISystem.h"

#include "ScreenGatekeeper.h" // ScreenGateResult 完全定義（std::future<ScreenGateResult> メンバのため）

#include <chrono>
#include <future>
#include <memory>

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

    // 重い Evaluate（全ウィンドウ列挙＋画面差分）をワーカースレッドへ退避。
    // 間隔ごとに 1 評価のみ起動（evalBusy_）＝ScreenGatekeeper をワーカーが排他使用する。
    std::future<ScreenGateResult> evalFuture_;
    bool evalBusy_ = false;
};
