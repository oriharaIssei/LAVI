#pragma once

#include <future>
#include <string>

#include "VisionAnalyzer.h" // VisionResult / future の完全型に必要

struct SharedMediaContext;

class VisionPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    SharedMediaContext* ctx_ = nullptr;

    VisionAnalyzer* visionAnalyzer_ = nullptr; // VisionSystem 所有。Draw 冒頭で ctx から pull（非所有）
    std::string visionResult_;
    std::future<VisionResult> visionFuture_;
    bool isVisionAnalyzing_ = false;
    int visionSource_ = 0;
};
