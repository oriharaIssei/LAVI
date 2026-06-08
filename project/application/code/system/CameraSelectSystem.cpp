#include "CameraSelectSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "GatekeeperConfig.h"
#include "system/component/ui/UIComboComponent.h"

#include "component/ComponentArray.h"
#include "mediaCapture/WebCamera.h"
#include "util/StringUtil.h"

CameraSelectSystem::CameraSelectSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

CameraSelectSystem::~CameraSelectSystem() = default;

void CameraSelectSystem::Update() {
    // 初回のみデバイス列挙（WebCameraSystem の StaticInitialize 後に走る Update で実施）。
    if (!enumerated_) {
        deviceIds_.clear();
        deviceNames_.clear();
        for (const auto& dev : OriGine::WebCamera::EnumerateDevices()) {
            deviceIds_.push_back(dev.id);
            deviceNames_.push_back(ConvertString(dev.name));
        }
        currentDeviceIndex_ = LoadGatekeeperConfig().camDeviceIndex;
        enumerated_ = true;
    }

    auto* arr = GetComponentArray<UIComboComponent>();
    if (!arr) {
        return;
    }
    for (auto& slot : arr->GetSlotsRef()) {
        for (auto& combo : slot.components) {
            if (combo.role != "webcam") {
                continue;
            }

            // 選択肢: 先頭に「既定デバイス」、続けて接続カメラ。
            combo.items.clear();
            combo.items.reserve(deviceNames_.size() + 1);
            combo.items.push_back("既定デバイス");
            for (const auto& name : deviceNames_) {
                combo.items.push_back(name);
            }

            if (combo.requestedIndex >= 0) {
                const int idx = combo.requestedIndex;
                const int deviceIndex = (idx == 0) ? -1 : (idx - 1);
                currentDeviceIndex_ = deviceIndex;

                GatekeeperConfigData cfg = LoadGatekeeperConfig();
                cfg.camDeviceIndex = deviceIndex;
                SaveGatekeeperConfig(cfg);

                // 撮影中でもデバイスを切り替える（停止→開き直し）。
                if (OriGine::WebCamera* cam = LaviContext::Get().webCamera) {
                    if (cam->IsCapturing()) {
                        cam->StopCapture();
                        cam->Close();
                    }
                    std::wstring id; // 空=既定
                    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(deviceIds_.size())) {
                        id = deviceIds_[deviceIndex];
                    }
                    if (cam->Open(id, 640, 480)) {
                        cam->StartCapture();
                    }
                }
                combo.selectedIndex  = idx;
                combo.requestedIndex = -1;
            } else {
                combo.selectedIndex = (currentDeviceIndex_ < 0) ? 0 : (currentDeviceIndex_ + 1);
            }
        }
    }
}
