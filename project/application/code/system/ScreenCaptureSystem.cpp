#include "ScreenCaptureSystem.h"

#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturedFrameComponent.h"
#include "system/component/ui/UIComboComponent.h"

#include "mediaCapture/ScreenCapture.h" // OriGine::ScreenCapture 完全定義
#include "component/ComponentArray.h"
#include "util/StringUtil.h" // ConvertString(wstring->utf8)
#include "Engine.h"
#include "directX12/DxCommand.h"
#include "directX12/DxDevice.h"

#include <chrono>
#include <memory>

ScreenCaptureSystem::ScreenCaptureSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {} // 環境入力（画面）の供給

ScreenCaptureSystem::~ScreenCaptureSystem() = default;

void ScreenCaptureSystem::RefreshMonitors() {
    monitors_      = OriGine::ScreenCapture::EnumerateMonitors();
    monitorsLoaded_ = true;
}

void ScreenCaptureSystem::OpenMonitor(uint32_t _monitorIndex) {
    OriGine::Engine* engine = OriGine::Engine::GetInstance();
    auto cap = std::make_unique<OriGine::ScreenCapture>();
    if (cap->Open(engine->GetDxDevice(), engine->GetDxCommand(), _monitorIndex)) {
        // 新フレーム到着をキャプチャスレッドから受け取る（dirty フラグのみ＝再入無し・アトミック）。
        cap->SetFrameCallback([this](const OriGine::ScreenFrame&) {
            frameDirty_.store(true, std::memory_order_relaxed);
        });
        cap->StartCapture();
        captures_.push_back(std::move(cap));
    }
}

void ScreenCaptureSystem::PublishContext() {
    SharedMediaContext& ctx = LaviContext::Get();
    ctx.screenCaptures.clear();
    ctx.screenCaptures.reserve(captures_.size());
    for (auto& c : captures_) {
        ctx.screenCaptures.push_back(c.get());
    }
    // 先頭を主モニタ（screen-diff 用）として公開。無ければ null。
    ctx.screenCapture = captures_.empty() ? nullptr : captures_.front().get();
    if (captures_.empty()) {
        ctx.screenFrame.reset(); // 撮影対象が無くなったら古いスナップショットを破棄
        ctx.screenFrames.clear();
    }
}

void ScreenCaptureSystem::ApplySelection(int _comboIndex) {
    selectedComboIndex_ = _comboIndex;

    // 既存キャプチャを全停止・破棄してから開き直す。
    for (auto& c : captures_) {
        c->StopCapture();
        c->Close();
    }
    captures_.clear();

    if (_comboIndex == 1) {
        // すべてのモニタを同時にキャプチャ。
        for (const auto& m : monitors_) {
            OpenMonitor(m.index);
        }
    } else if (_comboIndex >= 2 && (_comboIndex - 2) < static_cast<int>(monitors_.size())) {
        // 単一モニタ。
        OpenMonitor(monitors_[_comboIndex - 2].index);
    }
    // _comboIndex == 0（共有しない）は captures_ 空のまま。

    PublishContext();
}

void ScreenCaptureSystem::PublishFrame() {
    SharedMediaContext& ctx = LaviContext::Get();
    OriGine::ScreenCapture* primary = ctx.screenCapture; // 主モニタ（captures_.front 相当）
    if (!primary || !primary->IsCapturing()) {
        return;
    }

    auto frame = std::make_shared<CapturedFrame>();
    uint32_t w = 0, h = 0;
    if (!(primary->GetLatestFrame(frame->pixels, w, h) && w > 0 && h > 0)) {
        return;
    }
    frame->width  = w;
    frame->height = h;
    frame->seq    = ++frameSeq_;
    frame->time   = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    frame->source = 1;

    std::shared_ptr<const CapturedFrame> snapshot = std::move(frame);
    ctx.screenFrame = snapshot;
    ctx.screenFrames.assign(1, snapshot); // 現状の消費者は主モニタのみ参照
    if (auto* c = GetComponent<CapturedFrameComponent>(frameEntity_)) {
        c->frame = snapshot;
    }
    history_.Push(snapshot); // 時系列リングへ取り込む（大画面は内部で縮小保持）
}

void ScreenCaptureSystem::Initialize() {
    RefreshMonitors();
    PublishContext(); // 空で公開（ctx ポインタ初期化）

    // フレームスナップショットを保持するユニークエンティティ（ECS データレコード）。
    frameEntity_ = CreateEntity("ScreenFrame", true);
    AddComponent<CapturedFrameComponent>(frameEntity_);
    if (auto* c = GetComponent<CapturedFrameComponent>(frameEntity_)) {
        c->source = 1; // Screen
    }

    // 「動画的解釈」用の時系列リングを設定して公開する。大画面はメモリ保護のため長辺 1280 に縮小保持。
    SharedMediaContext& ctx = LaviContext::Get();
    history_.SetWindowSeconds(ctx.config.visionVideoWindowSec);
    history_.SetRetainFps(6.0f);
    history_.SetMaxFrames(8);
    history_.SetMaxLongSide(1280);
    history_.Clear();
    ctx.screenHistory = &history_;

    // screen-diff を使う設定のときのみ既定（先頭）モニタを開いて撮影開始する。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    if (cfg.screenEnabled && cfg.useScreenDiff && !monitors_.empty()) {
        ApplySelection(2); // 先頭モニタ（コンボ index 2 = monitors_[0]）
    }
}

void ScreenCaptureSystem::Update() {
    // 新フレームが来ているときだけ主モニタを 1 回コピーしてスナップショット発行（無駄コピー回避）。
    if (frameDirty_.exchange(false)) {
        PublishFrame();
    }

    auto* arr = GetComponentArray<UIComboComponent>();
    if (!arr) {
        return;
    }
    for (auto& slot : arr->GetSlotsRef()) {
        for (auto& combo : slot.components) {
            if (combo.role != "screen") {
                continue;
            }

            // 選択肢: 「共有しない」「すべてのモニタ」、続けて各モニタ名（解像度付き）。
            combo.items.clear();
            combo.items.reserve(monitors_.size() + 2);
            combo.items.push_back("共有しない");
            combo.items.push_back("すべてのモニタ");
            for (const auto& m : monitors_) {
                std::string label = ConvertString(m.name);
                label += " (" + std::to_string(m.width) + "x" + std::to_string(m.height) + ")";
                combo.items.push_back(label);
            }

            if (combo.requestedIndex >= 0) {
                ApplySelection(combo.requestedIndex);
                combo.selectedIndex  = selectedComboIndex_;
                combo.requestedIndex = -1;
            } else {
                combo.selectedIndex = selectedComboIndex_;
            }
        }
    }
}

void ScreenCaptureSystem::Finalize() {
    for (auto& c : captures_) {
        c->StopCapture();
        c->Close();
    }
    captures_.clear();
    SharedMediaContext& ctx = LaviContext::Get();
    ctx.screenCapture = nullptr;
    ctx.screenCaptures.clear();
    ctx.screenFrame.reset();
    ctx.screenFrames.clear();
    ctx.screenHistory = nullptr;
    history_.Clear();
}
