#include "WebCameraSystem.h"

#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"

#include "mediaCapture/WebCamera.h" // OriGine::WebCamera 完全定義

#include <vector>

WebCameraSystem::WebCameraSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {} // 環境入力（映像）の供給

WebCameraSystem::~WebCameraSystem() = default;

void WebCameraSystem::Initialize() {
    OriGine::WebCamera::StaticInitialize();
    staticInitialized_ = true;

    webCamera_ = std::make_unique<OriGine::WebCamera>();
    LaviContext::Get().webCamera = webCamera_.get();

    // カメラ有効時のみ設定デバイスを開いて撮影開始する（既定 OFF＝プライバシー安全側）。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    if (cfg.camEnabled) {
        auto devices = OriGine::WebCamera::EnumerateDevices();
        std::wstring deviceId; // 空=既定デバイス
        if (cfg.camDeviceIndex >= 0 && cfg.camDeviceIndex < static_cast<int>(devices.size())) {
            deviceId = devices[cfg.camDeviceIndex].id;
        }
        if (webCamera_->Open(deviceId, 640, 480)) {
            webCamera_->StartCapture();
        }
    }
}

void WebCameraSystem::Finalize() {
    if (webCamera_) {
        webCamera_->StopCapture();
        webCamera_->Close();
    }
    LaviContext::Get().webCamera = nullptr;
    webCamera_.reset();

    if (staticInitialized_) {
        OriGine::WebCamera::StaticFinalize();
        staticInitialized_ = false;
    }
}
