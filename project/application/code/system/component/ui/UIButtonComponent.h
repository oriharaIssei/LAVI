#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <Vector4.h>
#include <string>

/// <summary>
/// ボタンウィジェット。UIElementComponent と同居する。
/// クリックされると actionId をキーに UIActionRegistry のラムダを UIInputSystem が発火する。
/// 通常 / ホバー / 押下の色は UIPresentationSystem が同居 SpriteRenderer へ反映する。
/// </summary>
class UIButtonComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UIButtonComponent& c);
    friend void from_json(const nlohmann::json& j, UIButtonComponent& c);

public:
    UIButtonComponent()           = default;
    ~UIButtonComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    // --- 直列化対象 ---
    std::string label;                                // 表示ラベル（同居 TextComponent へ反映）
    std::string actionId;                             // UIActionRegistry のキー
    OriGine::Vec4f color      = {0.20f, 0.22f, 0.28f, 1.0f}; // 通常色
    OriGine::Vec4f hoverColor = {0.30f, 0.34f, 0.42f, 1.0f}; // ホバー色
    OriGine::Vec4f pressColor = {0.12f, 0.40f, 0.70f, 1.0f}; // 押下色

    // --- 実行時状態（非直列化）---
    bool clickedThisFrame = false; // 押下→解放がこのフレームで成立したか
};
