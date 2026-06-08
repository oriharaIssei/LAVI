#pragma once

#include "system/ISystem.h"

/// <summary>
/// UI の入力（ヒットテスト・クリック・ドラッグ・タブ切替・アクション発火）を担う ECS システム
/// （Category=Input）。Scene の MouseInput からマウス座標とボタン状態を取得し、interactive な
/// UIElementComponent を worldPos/size で判定して hovered/pressed を更新する。
/// - Button: 押下→解放成立で UIActionRegistry の actionId を発火。
/// - Slider: ドラッグで value を更新し UIBindingRegistry 経由で実値へ書き戻す。
/// - TabBar: タブ区画クリックで activeIndex を更新。
///
/// MediaCaptureDemoSystem の ImGui 即時 UI を ECS リテインドモード UI へ置き換えるフレームワークの一部。
/// </summary>
class UIInputSystem : public OriGine::ISystem {
public:
    UIInputSystem();
    ~UIInputSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;
};
