#include "WebCameraSystem.h"

#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturedFrameComponent.h"

#include "mediaCapture/WebCamera.h" // OriGine::WebCamera 完全定義

#include <chrono>
#include <memory>
#include <vector>

WebCameraSystem::WebCameraSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {} // 環境入力（映像）の供給

WebCameraSystem::~WebCameraSystem() = default;

void WebCameraSystem::Initialize() {
    OriGine::WebCamera::StaticInitialize();
    staticInitialized_ = true;

    webCamera_ = std::make_unique<OriGine::WebCamera>();
    LaviContext::Get().webCamera = webCamera_.get();

    // 新フレーム到着をキャプチャスレッドから受け取る（dirty フラグのみ立てる＝再入無し・アトミック）。
    webCamera_->SetFrameCallback([this](const OriGine::CameraFrame&) {
        frameDirty_.store(true, std::memory_order_relaxed);
    });

    // フレームスナップショットを保持するユニークエンティティ（ECS データレコード）。
    frameEntity_ = CreateEntity("CameraFrame", true);
    AddComponent<CapturedFrameComponent>(frameEntity_);
    if (auto* c = GetComponent<CapturedFrameComponent>(frameEntity_)) {
        c->source = 0; // Camera
    }

    // 「動画的解釈」用の時系列リングを設定して公開する（窓は設定値、保持は控えめに間引き）。
    SharedMediaContext& ctx = LaviContext::Get();
    history_.SetWindowSeconds(ctx.config.visionVideoWindowSec);
    history_.SetRetainFps(8.0f);
    history_.SetMaxFrames(16);   // 640x480 は軽いので余裕を持たせる
    history_.SetMaxLongSide(0);  // カメラは縮小不要
    history_.Clear();
    ctx.cameraHistory = &history_;

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

void WebCameraSystem::Update() {
    if (!webCamera_) {
        return;
    }
    // 新フレームが来ていなければ何もしない（無駄な全フレームコピーを避ける）。
    if (!frameDirty_.exchange(false)) {
        return;
    }
    if (!webCamera_->IsCapturing()) {
        return;
    }

    auto frame   = std::make_shared<CapturedFrame>();
    uint32_t w = 0, h = 0;
    if (!(webCamera_->GetLatestFrame(frame->pixels, w, h) && w > 0 && h > 0)) {
        return;
    }
    frame->width  = w;
    frame->height = h;
    frame->seq    = ++frameSeq_;
    frame->time   = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    frame->source = 0;

    std::shared_ptr<const CapturedFrame> snapshot = std::move(frame);

    SharedMediaContext& ctx = LaviContext::Get();
    ctx.cameraFrame = snapshot;
    ctx.camFrameW   = w; // 寸法公開を維持（顔識別 UI 用）
    ctx.camFrameH   = h;
    if (auto* c = GetComponent<CapturedFrameComponent>(frameEntity_)) {
        c->frame = snapshot;
    }
    history_.Push(snapshot); // 時系列リングへ取り込む
}

void WebCameraSystem::Finalize() {
    if (webCamera_) {
        webCamera_->StopCapture();
        webCamera_->Close();
    }
    SharedMediaContext& ctx = LaviContext::Get();
    ctx.webCamera   = nullptr;
    ctx.cameraFrame.reset();
    ctx.cameraHistory = nullptr;
    history_.Clear();
    webCamera_.reset();

    if (staticInitialized_) {
        OriGine::WebCamera::StaticFinalize();
        staticInitialized_ = false;
    }
}
