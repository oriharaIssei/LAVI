#pragma once

#include "system/ISystem.h"

#include "mediaCapture/ScreenCapture.h" // OriGine::ScreenCapture / ScreenMonitorInfo 完全定義

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
    // コンボ選択(0="共有しない", k>=1=モニタ)を実際のキャプチャ対象へ反映する。
    void ApplySelection(int _comboIndex);

    std::unique_ptr<OriGine::ScreenCapture> screenCapture_;
    std::vector<OriGine::ScreenMonitorInfo> monitors_;
    int selectedComboIndex_ = 0;  // 0 = 共有しない
    bool monitorsLoaded_    = false;
};
