#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <string>

/// <summary>
/// 水平スライダーウィジェット。UIElementComponent と同居する。
/// ドラッグで value を [minValue, maxValue] 内で更新し、bindKey をキーに
/// UIBindingRegistry 経由で実値（config / 状態フィールド）へ書き戻す（UIInputSystem が駆動）。
/// 塗り量・ハンドル位置は UIPresentationSystem が同居 SpriteRenderer へ反映する。
/// </summary>
class UISliderComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UISliderComponent& c);
    friend void from_json(const nlohmann::json& j, UISliderComponent& c);

public:
    UISliderComponent()           = default;
    ~UISliderComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    /// <summary>0..1 に正規化した現在値</summary>
    float Normalized() const {
        const float range = maxValue - minValue;
        if (range <= 0.0f) return 0.0f;
        float t = (value - minValue) / range;
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    // --- 直列化対象 ---
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value    = 0.0f;
    std::string bindKey; // UIBindingRegistry のキー
    std::string label;   // 表示ラベル（同居 TextComponent へ反映）

    // --- 実行時状態（非直列化）---
    bool dragging = false;
};
