#pragma once

#include "system/ISystem.h"

#include "mediaCapture/ScreenCapture.h" // OriGine::ScreenCapture / ScreenMonitorInfo 完全定義

#include "FrameHistory.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

/// <summary>
/// 画面キャプチャのライフサイクルを管理する ECS システム（Category=Input）。
/// ScreenCapture を所有して LaviContext::Get().screenCapture に公開する。ScreenGateSystem の
/// 画面差分(screen-diff)評価がフレームを参照する（フォアグラウンド/新規起動検知はフレーム不要）。
/// 旧 ScreenCapturePanel が DEBUG 限定＋ボタン依存だったため Release で画面差分が動かなかった問題の解消（ECS 分解）。
/// キャプチャ起動は GatekeeperConfig の screenEnabled && useScreenDiff でゲートする
/// （screen-diff を使う設定のときのみ既定モニタを開いて撮影開始）。調整 UI は ctx.screenCapture を pull する。
/// </summary>
class ScreenCaptureSystem : public OriGine::ISystem {
public:
    ScreenCaptureSystem();
    ~ScreenCaptureSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    // role=="screen" の UIComboComponent を駆動（モニタ一覧供給＋選択でキャプチャ対象を切替）。
    void Update() override;

private:
    void RefreshMonitors();
    // コンボ選択(0="共有しない", 1="すべてのモニタ", k>=2=monitors_[k-2])を実際のキャプチャ対象へ反映する。
    void ApplySelection(int _comboIndex);
    void OpenMonitor(uint32_t _monitorIndex); // 1 モニタを開いて撮影開始し captures_ に追加
    void PublishContext();                    // ctx.screenCapture / screenCaptures を更新

    void PublishFrame(); // 新フレーム時に主モニタのスナップショットを ctx / Component へ発行

    std::vector<std::unique_ptr<OriGine::ScreenCapture>> captures_; // 撮影中モニタ（複数同時可）
    std::vector<OriGine::ScreenMonitorInfo> monitors_;
    int selectedComboIndex_ = 0;  // 0 = 共有しない
    bool monitorsLoaded_    = false;

    // フレーム供給（プロデューサ）。frameDirty_ は各キャプチャのコールバックから立つ。
    std::atomic<bool> frameDirty_{false};
    uint64_t frameSeq_ = 0;
    OriGine::EntityHandle frameEntity_; // 主モニタの CapturedFrameComponent を持つユニークエンティティ

    FrameHistory history_; // 「動画的解釈」用の時系列リング（主モニタ・大画面は縮小保持）
};
