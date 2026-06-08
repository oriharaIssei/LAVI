#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PreviewTextureUtil.h"
#include "mediaCapture/WebCamera.h"

struct SharedMediaContext;

class WebCameraPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    SharedMediaContext* ctx_ = nullptr;

    // WebCamera の所有・起動は WebCameraSystem。本パネルは ctx.webCamera を pull する DEBUG ビューア。
    OriGine::WebCamera* webCamera_ = nullptr; // 非所有（Draw 入口で ctx から pull）
    std::vector<OriGine::WebCameraDeviceInfo> camDevices_;
    int selectedCamDevice_ = 0;
    PreviewTexture camPreview_;
};
