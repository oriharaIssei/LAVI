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

void ScreenCaptureSystem::ApplySelection(int _comboIndex) {
    if (!screenCapture_) {
        return;
    }
    selectedComboIndex_ = _comboIndex;

    // 一旦停止してから開き直す（0=共有しない、k>=1=monitors_[k-1]）。
    screenCapture_->StopCapture();
    screenCapture_->Close();

    if (_comboIndex >= 1 && (_comboIndex - 1) < static_cast<int>(monitors_.size())) {
        OriGine::Engine* engine        = OriGine::Engine::GetInstance();
        const uint32_t monitorIndex    = monitors_[_comboIndex - 1].index;
        if (screenCapture_->Open(engine->GetDxDevice(), engine->GetDxCommand(), monitorIndex)) {
            screenCapture_->StartCapture();
        }
    }
}

void ScreenCaptureSystem::Initialize() {
    screenCapture_                   = std::make_unique<OriGine::ScreenCapture>();
    LaviContext::Get().screenCapture = screenCapture_.get();

    RefreshMonitors();

    // screen-diff を使う設定のときのみ既定モニタを開いて撮影開始する。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    if (cfg.screenEnabled && cfg.useScreenDiff) {
        OriGine::Engine* engine = OriGine::Engine::GetInstance();
        if (screenCapture_->Open(engine->GetDxDevice(), engine->GetDxCommand(), 0)) {
            screenCapture_->StartCapture();
            selectedComboIndex_ = 1; // 既定モニタ（一覧の先頭）を共有中
        }
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

            // 選択肢: 先頭に「共有しない」、続けて各モニタ名（解像度付き）。
            combo.items.clear();
            combo.items.reserve(monitors_.size() + 1);
            combo.items.push_back("共有しない");
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
    if (screenCapture_) {
        screenCapture_->StopCapture();
        screenCapture_->Close();
    }
    LaviContext::Get().screenCapture = nullptr;
    screenCapture_.reset();
}
