#pragma once

#include "component/IComponent.h" // engine ECS (engine/code/ECS は include パス上)

#include <nlohmann/json.hpp>
#include <Vector2.h>
#include <string>

/// <summary>
/// UI ウィジェット共通の「矩形・アンカー・階層・可視/対話状態」を表す Component。
/// レイアウトは UILayoutSystem が parentUuid を辿って worldPos を解決する（親矩形 + anchor + offsetPos）。
/// 見た目は同居する SpriteRenderer(背景) / TextComponent(ラベル) が担い、UIPresentationSystem が状態を反映する。
/// 入力判定は UIInputSystem が worldPos/size でヒットテストする。
///
/// MediaCaptureDemoSystem の ImGui 即時 UI を ECS リテインドモード UI へ置き換えるフレームワークの基礎。
/// </summary>
class UIElementComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UIElementComponent& c);
    friend void from_json(const nlohmann::json& j, UIElementComponent& c);

public:
    UIElementComponent()           = default;
    ~UIElementComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    /// <summary>スクリーン座標 _p が解決後の矩形内にあるか</summary>
    bool HitTest(const OriGine::Vec2f& _p) const {
        return _p[0] >= worldPos[0] && _p[0] <= worldPos[0] + size[0]
            && _p[1] >= worldPos[1] && _p[1] <= worldPos[1] + size[1];
    }

    // --- 直列化対象（権威データ）---
    OriGine::Vec2f anchor    = {0.0f, 0.0f};    // 親矩形内のアンカー(0..1)
    OriGine::Vec2f offsetPos = {0.0f, 0.0f};    // アンカー点からの px オフセット
    OriGine::Vec2f size      = {100.0f, 32.0f}; // px サイズ
    std::string parentUuid;                     // 親エンティティ uuid（空 = ルート、画面基準）
    bool visible     = true;
    bool interactive = false;

    // --- 実行時状態（非直列化）---
    OriGine::Vec2f worldPos = {0.0f, 0.0f}; // 解決後の左上スクリーン座標
    bool effectiveVisible   = true;         // 祖先の可視を畳んだ結果
    bool hovered            = false;
    bool pressed            = false;
};
