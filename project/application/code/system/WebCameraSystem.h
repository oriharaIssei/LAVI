#pragma once

#include "system/ISystem.h"

#include <memory>

namespace OriGine { class WebCamera; }

/// <summary>
/// Web カメラデバイスのライフサイクルを管理する ECS システム（Category=Input）。
/// WebCamera を所有して LaviContext::Get().webCamera に公開し、Media Foundation の
/// StaticInitialize/Finalize も担う。CameraGateSystem（表情認識）と MemorySystem（顔識別）が
/// ctx.webCamera のフレームを参照する。
/// 旧 WebCameraPanel が DEBUG 限定＋「Open & Start Capture」ボタン依存だったため Release で
/// カメラが起動しなかった問題の解消（ECS 分解）。
/// キャプチャ起動は GatekeeperConfig の camEnabled でゲートする（既定 OFF＝プライバシー安全側。
/// 有効時のみ既定デバイスを 640x480 で開いて撮影開始）。調整 UI（WebCameraPanel）は ctx.webCamera を pull する。
/// </summary>
class WebCameraSystem : public OriGine::ISystem {
public:
    WebCameraSystem();
    ~WebCameraSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // キャプチャは内部スレッド駆動。フレーム取得は消費側が行う

private:
    std::unique_ptr<OriGine::WebCamera> webCamera_;
    bool staticInitialized_ = false;
};
