#include "ScreenCaptureSystem.h"

#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/ui/UIComboComponent.h"

#include "mediaCapture/ScreenCapture.h" // OriGine::ScreenCapture 完全定義
#include "component/ComponentArray.h"
#include "util/StringUtil.h" // ConvertString(wstring->utf8)
#include "Engine.h"
#include "directX12/DxCommand.h"
#include "directX12/DxDevice.h"

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

void ScreenCaptureSystem::Initialize() {
    RefreshMonitors();
    PublishContext(); // 空で公開（ctx ポインタ初期化）

    // screen-diff を使う設定のときのみ既定（先頭）モニタを開いて撮影開始する。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    if (cfg.screenEnabled && cfg.useScreenDiff && !monitors_.empty()) {
        ApplySelection(2); // 先頭モニタ（コンボ index 2 = monitors_[0]）
    }
}

void ScreenCaptureSystem::Update() {
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
}
