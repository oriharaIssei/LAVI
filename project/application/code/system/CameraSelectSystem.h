#pragma once

#include "system/ISystem.h"

#include <string>
#include <vector>

/// <summary>
/// Web カメラ選択ドロップダウン（UIComboComponent role="webcam"）のドライバ ECS システム
/// （Category=StateTransition）。「既定デバイス」＋接続中カメラを選択肢として供給し、選択されたら
/// GatekeeperConfig.camDeviceIndex に永続化して ctx.webCamera のデバイスを切り替える。
/// デバイス列挙は WebCameraSystem の StaticInitialize 後に行う必要があるため初回 Update で実施する。
/// </summary>
class CameraSelectSystem : public OriGine::ISystem {
public:
    CameraSelectSystem();
    ~CameraSelectSystem() override;

    void Initialize() override {}
    void Finalize() override {}

protected:
    void Update() override;

private:
    std::vector<std::wstring> deviceIds_;   // 接続カメラのデバイス ID
    std::vector<std::string>  deviceNames_; // 表示名（UTF-8）
    int currentDeviceIndex_ = -1;           // 現在の選択（-1=既定）
    bool enumerated_ = false;
};
