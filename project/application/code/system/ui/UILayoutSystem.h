#pragma once

#include "system/ISystem.h"

/// <summary>
/// UI の階層・矩形・可視を解決する ECS システム（Category=StateTransition）。
/// UIElementComponent を持つ全エンティティを GetComponentArray で走査し、parentUuid を辿って
/// worldPos（左上スクリーン座標）と effectiveVisible（祖先可視の畳み込み）を毎フレーム解決する。
/// UITabBarComponent.activeIndex に応じて UIPageComponent の可視を切り替え、子へ伝播させる。
///
/// MediaCaptureDemoSystem の ImGui 即時 UI を ECS リテインドモード UI へ置き換えるフレームワークの一部。
/// </summary>
class UILayoutSystem : public OriGine::ISystem {
public:
    UILayoutSystem();
    ~UILayoutSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;
};
