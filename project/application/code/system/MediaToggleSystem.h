#pragma once

#include "system/ISystem.h"

/// <summary>
/// マイク / 画面共有 / Web カメラの「使用する」チェックボックス（UICheckboxComponent）のドライバ
/// ECS システム（Category=StateTransition: Input のクリックを受けて Render の前に反映）。
///
/// role により対象を判別する：
///   "mic.enabled"    → GatekeeperConfig.micEnabled
///   "screen.enabled" → GatekeeperConfig.screenEnabled
///   "camera.enabled" → GatekeeperConfig.camEnabled
///
/// クリック（toggleRequested）で当該フラグを反転し、GatekeeperConfig へ永続化したうえで、
/// 動作中の GatekeeperManager::config() にも即時反映する（各 GateSystem は毎フレーム config を
/// 参照するため、その場でゲート評価の ON/OFF が切り替わる）。非クリック時は現在値へ checked を同期。
/// </summary>
class MediaToggleSystem : public OriGine::ISystem {
public:
    MediaToggleSystem();
    ~MediaToggleSystem() override;

    void Initialize() override {}
    void Finalize() override {}

protected:
    void Update() override;
};
