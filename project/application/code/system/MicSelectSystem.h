#pragma once

#include "system/ISystem.h"

#include <string>
#include <vector>

/// <summary>
/// マイク選択ドロップダウン（UIComboComponent role="microphone"）のドライバ ECS システム
/// （Category=StateTransition: Input の開閉/選択を受けて Render の前に反映）。
/// 「Windows 規定」＋接続中マイクを選択肢として供給し、選択されたら MicrophoneConfig に永続化して
/// MicrophonePanel(ctx.micPanel) のデバイスを切り替える。
/// </summary>
class MicSelectSystem : public OriGine::ISystem {
public:
    MicSelectSystem();
    ~MicSelectSystem() override;

    void Initialize() override;
    void Finalize() override {}

protected:
    void Update() override;

private:
    void RefreshDevices();

    std::vector<std::string> deviceNames_; // 接続中マイクの表示名（UTF-8）
    int currentDeviceIndex_ = -1;          // 現在の選択（-1=Windows 規定、0..=デバイス番号）
    bool initialized_ = false;
};
