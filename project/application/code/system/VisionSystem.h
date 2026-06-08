#pragma once

#include "system/ISystem.h"

#include <memory>

class VisionAnalyzer;

/// <summary>
/// 画像解析（Claude Vision / VisionAnalyzer）を担う ECS システム。
/// VisionAnalyzer を所有し、初期化時に config（apiKey / visionPrompt）を適用して
/// LaviContext に公開する。消費側（VisionPanel の DEBUG UI / 将来の自律解析）は
/// LaviContext::Get().visionAnalyzer を参照する。
///
/// MediaCaptureDemoSystem（VisionPanel 所有）からの切り出し（ECS 分解）。
/// パターンは KnowledgeSystem / ActionSystem と同じ（Initialize で生成・公開のみ）。
/// </summary>
class VisionSystem : public OriGine::ISystem {
public:
    VisionSystem();
    ~VisionSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // 解析は要求時に AnalyzeAsync。毎フレーム処理は不要。

private:
    std::unique_ptr<VisionAnalyzer> visionAnalyzer_;
};
