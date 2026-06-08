#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <string>

/// <summary>
/// チェックボックス（ON/OFF トグル）ウィジェット。UIElementComponent と同居する。
/// クリックで toggleRequested を立て、role を見たドライバ System が消費して
/// 設定の反映・永続化と checked 状態の同期を行う（UIComboComponent と同じ role 駆動設計）。
/// UIInputSystem がクリックを処理し、UIPresentationSystem が "[x]/[ ] label" を
/// 同居 SpriteRenderer/TextComponent へ反映する。
/// </summary>
class UICheckboxComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UICheckboxComponent& c);
    friend void from_json(const nlohmann::json& j, UICheckboxComponent& c);

public:
    UICheckboxComponent()           = default;
    ~UICheckboxComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    // --- 直列化対象 ---
    std::string label; // 表示ラベル（同居 TextComponent へ "[x] label" の形で反映）
    std::string role;  // 設定の読み書きを担うドライバ System の識別子（例: "mic.enabled"）

    // --- 実行時状態（非直列化）---
    bool checked         = false; // 現在の ON/OFF（ドライバが毎フレーム設定値に同期）
    bool toggleRequested = false; // クリックで立つ。ドライバが消費して false に戻す
};
