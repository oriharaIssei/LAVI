#pragma once

#include "system/ISystem.h"

#include "FrameHistory.h"

#include <atomic>
#include <cstdint>
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
    // 新フレーム到着時のみ 1 回だけ GetLatestFrame して不変スナップショットを発行する
    // （ctx.cameraFrame と CapturedFrameComponent）。各消費者の個別 GetLatestFrame を置き換える。
    void Update() override;

private:
    std::unique_ptr<OriGine::WebCamera> webCamera_;
    bool staticInitialized_ = false;

    // フレーム供給（プロデューサ）。frameDirty_ はキャプチャスレッドのコールバックから立つ。
    std::atomic<bool> frameDirty_{false};
    uint64_t frameSeq_ = 0;
    OriGine::EntityHandle frameEntity_; // CapturedFrameComponent を持つユニークエンティティ

    FrameHistory history_; // 「動画的解釈」用の時系列リング
};
