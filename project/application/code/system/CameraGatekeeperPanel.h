#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CameraGatekeeper.h"

struct SharedMediaContext;

class CameraGatekeeperPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    void Evaluate();

    SharedMediaContext* ctx_ = nullptr;
    std::unique_ptr<CameraGatekeeper> gk_;

    std::string ferModelPath_ = "application/resource/gatekeeper/emotion-ferplus-8.onnx";
    std::string haarPath_     = "application/resource/gatekeeper/haarcascade_frontalface_default.xml";
    bool useGpu_  = false;
    bool loaded_  = false;
    std::string status_;

    float threshold_      = 0.5f;
    bool autoMonitor_     = false;
    double lastEvalTime_  = 0.0;

    // 顔検出パラメータ
    float scaleFactor_ = 1.1f;
    int minNeighbors_  = 5;
    int minFaceSize_   = 80;

    // トリガー安定化
    int confirmFrames_    = 1;
    bool ignoreNeutral_   = false;
    float cooldownSec_    = 1.0f;
    float neutralBias_    = 0.0f;
    double lastTriggerTime_ = -1.0e9;

    EmotionResult lastResult_;
    std::vector<std::string> triggerLog_;
};
