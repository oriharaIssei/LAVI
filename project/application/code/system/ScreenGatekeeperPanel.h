#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ScreenGatekeeper.h"

struct SharedMediaContext;

class ScreenGatekeeperPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    void Evaluate();

    SharedMediaContext* ctx_ = nullptr;
    std::unique_ptr<ScreenGatekeeper> gk_;

    bool watchForeground_ = true;
    bool detectNew_       = true;
    bool useScreenDiff_   = true;
    float screenDiffThreshold_ = 0.05f;
    int pixelDiffThreshold_    = 25;

    bool autoMonitor_   = false;
    double lastEvalTime_ = 0.0;
    float intervalSec_  = 0.5f;
    float cooldownSec_  = 1.0f;
    double lastTriggerTime_ = -1.0e9;

    ScreenGateResult lastResult_;
    std::vector<std::string> triggerLog_;
};
