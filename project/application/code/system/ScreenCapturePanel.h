#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PreviewTextureUtil.h"
#include "mediaCapture/ScreenCapture.h"

struct SharedMediaContext;

class ScreenCapturePanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    SharedMediaContext* ctx_ = nullptr;

    std::unique_ptr<OriGine::ScreenCapture> screenCapture_;
    std::vector<OriGine::ScreenMonitorInfo> monitors_;
    int selectedMonitor_ = 0;
    PreviewTexture screenPreview_;
};
