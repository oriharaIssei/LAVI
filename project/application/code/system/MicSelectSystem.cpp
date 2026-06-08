#include "MicSelectSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "MicrophonePanel.h"
#include "MicrophoneConfig.h"
#include "system/component/ui/UIComboComponent.h"

#include "component/ComponentArray.h"
#include "mediaCapture/Microphone.h"
#include "util/StringUtil.h"

MicSelectSystem::MicSelectSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

MicSelectSystem::~MicSelectSystem() = default;

void MicSelectSystem::RefreshDevices() {
    deviceNames_.clear();
    for (const auto& dev : OriGine::Microphone::EnumerateDevices()) {
        deviceNames_.push_back(ConvertString(dev.name));
    }
}

void MicSelectSystem::Initialize() {
    const MicrophoneConfigData cfg = LoadMicrophoneConfig();
    currentDeviceIndex_ = cfg.micDeviceIndex; // -1=既定
    RefreshDevices();
    initialized_ = true;
}

void MicSelectSystem::Update() {
    auto* arr = GetComponentArray<UIComboComponent>();
    if (!arr) {
        return;
    }
    for (auto& slot : arr->GetSlotsRef()) {
        const OriGine::EntityHandle owner = slot.owner;
        for (auto& combo : slot.components) {
            if (combo.role != "microphone") {
                continue;
            }
            (void)owner;

            // 選択肢: 先頭に「Windows 規定」、続けて接続中マイク。
            combo.items.clear();
            combo.items.reserve(deviceNames_.size() + 1);
            combo.items.push_back("Windows 規定（既定デバイス）");
            for (const auto& name : deviceNames_) {
                combo.items.push_back(name);
            }

            if (combo.requestedIndex >= 0) {
                // ユーザー確定 → デバイス番号へ写像（0=既定=-1, k>=1 → k-1）して反映・永続化。
                const int idx = combo.requestedIndex;
                const int deviceIndex = (idx == 0) ? -1 : (idx - 1);
                currentDeviceIndex_ = deviceIndex;

                MicrophoneConfigData cfg = LoadMicrophoneConfig();
                cfg.micDeviceIndex = deviceIndex;
                SaveMicrophoneConfig(cfg);

                if (MicrophonePanel* mic = LaviContext::Get().micPanel) {
                    mic->SwitchDevice(deviceIndex);
                }
                combo.selectedIndex  = idx;
                combo.requestedIndex = -1;
            } else {
                // 設定値に追従（既定=-1 → 0, k → k+1）。
                combo.selectedIndex = (currentDeviceIndex_ < 0) ? 0 : (currentDeviceIndex_ + 1);
            }
        }
    }
}
