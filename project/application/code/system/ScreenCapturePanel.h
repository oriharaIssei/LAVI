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

    // ScreenCapture の所有・起動は ScreenCaptureSystem。本パネルは ctx.screenCapture を pull する DEBUG ビューア。
    OriGine::ScreenCapture* screenCapture_ = nullptr; // 非所有（Draw 入口で ctx から pull）
    std::vector<OriGine::ScreenMonitorInfo> monitors_;
    int selectedMonitor_ = 0;
    PreviewTexture screenPreview_;
};
