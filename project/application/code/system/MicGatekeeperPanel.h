#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MicGatekeeper.h"

struct SharedMediaContext;

class MicGatekeeperPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    void ApplyKeywords();
    void Evaluate();

    SharedMediaContext* ctx_ = nullptr;
    std::unique_ptr<MicGatekeeper> gk_;

    std::string keywordText_ = "ねえ\nLAVI\n教えて";
    bool caseSensitive_ = false;
    bool autoMonitor_   = true;

    MicGateResult lastResult_;
    std::vector<std::string> triggerLog_;
};
