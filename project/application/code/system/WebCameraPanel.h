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

    std::unique_ptr<OriGine::WebCamera> webCamera_;
    std::vector<OriGine::WebCameraDeviceInfo> camDevices_;
    int selectedCamDevice_ = 0;
    PreviewTexture camPreview_;
};
