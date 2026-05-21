#pragma once

#include <future>
#include <memory>
#include <string>

#include "VisionAnalyzer.h"

struct SharedMediaContext;

class VisionPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    SharedMediaContext* ctx_ = nullptr;

    std::unique_ptr<VisionAnalyzer> visionAnalyzer_;
    std::string visionResult_;
    std::future<VisionResult> visionFuture_;
    bool isVisionAnalyzing_ = false;
    int visionSource_ = 0;
};
