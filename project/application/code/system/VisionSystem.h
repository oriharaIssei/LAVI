#pragma once

#include "system/ISystem.h"

#include <future>
#include <memory>

class VisionAnalyzer;
class LocalVisionAnalyzer;

/// <summary>
/// 画像解析を担う ECS システム。クラウド（VisionAnalyzer / Claude）と
/// ローカル（LocalVisionAnalyzer / Qwen2.5-VL + llama.cpp libmtmd）の両方を所有し、
/// LaviContext に公開する。消費側（VisionPanel / AutoObserveSystem）は
/// LaviContext::Get().visionAnalyzer / .localVisionAnalyzer を参照し、
/// GatekeeperConfig.visionUseLocal で切り替える。
///
/// ローカル VLM は VRAM を食うため、vision.useLocal が ON になって初めて
/// バックグラウンドで遅延ロードする（Update で監視）。
/// </summary>
class VisionSystem : public OriGine::ISystem {
public:
    VisionSystem();
    ~VisionSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override; // vision.useLocal 監視＝ローカル VLM の遅延ロード

private:
    std::unique_ptr<VisionAnalyzer> visionAnalyzer_;
    std::unique_ptr<LocalVisionAnalyzer> localVision_;
    std::future<bool> loadFuture_; // ローカル VLM の非同期ロード
    bool loadStarted_ = false;
};
