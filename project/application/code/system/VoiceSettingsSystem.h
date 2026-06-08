#pragma once

#include "system/ISystem.h"

/// <summary>
/// サウンド設定（VoiceVox 話者選択）の ECS UI ドライバ。
/// role="voicevox.speaker" のコンボに話者一覧（ctx.voiceVoxSpeakers）を供給し、
/// 選択を ctx.selectedSpeaker へ反映＋永続化する。実発話（GatekeeperManager::Speak）と
/// 手動チャットは ctx.selectedSpeaker を参照するため即時反映される。
///
/// 話者一覧は VoiceVox エンジン起動後に非同期取得されるため（VoiceVoxSystem）、
/// items の供給は Initialize ではなく Update で毎フレーム行う（CameraSelectSystem 踏襲）。
/// `mem:project_settings_overlay` / `mem:feedback_no_hardcode`。
/// </summary>
class VoiceSettingsSystem : public OriGine::ISystem {
public:
    VoiceSettingsSystem();
    ~VoiceSettingsSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;
};
