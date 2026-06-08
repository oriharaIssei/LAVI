#pragma once

#include "system/ISystem.h"

/// <summary>
/// LLM 設定（API キー / ローカル・クラウド切替 / Web Context）の ECS UI ドライバ。
///
/// - API キー: テキスト入力ウィジェット（actionId="llm.apikey.set"）の確定で
///   LaviContext::Get().config.apiKey を更新し AppConfig へ永続化する。クラウド GK は
///   送信時に毎回 config.apiKey を読むため即時反映される。Update で入力欄のプレースホルダを
///   設定状態（設定済み/未設定）に追従させる。
/// - ローカル使用 / Web Context: チェックボックス（role="llm.useLocal" / "llm.useWebContext"）
///   として MediaToggleSystem が GatekeeperConfig＋動作中 gkManager->config() に反映する。
///
/// `mem:feedback_no_hardcode` / `mem:project_settings_overlay` / `mem:project_ui_ecs_framework`。
/// </summary>
class LLMSettingsSystem : public OriGine::ISystem {
public:
    LLMSettingsSystem();
    ~LLMSettingsSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;
};
