#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/// <summary>
/// タブバーウィジェット。UIElementComponent と同居する。
/// 各タブは「ラベル」と「切り替え対象ページの uuid」を持つ。タブ矩形クリックで activeIndex を更新する
/// （UIInputSystem）。各 UIPageComponent は自身の index が activeIndex と一致するときだけ可視になる
/// （UILayoutSystem が反映）。タブは水平に等幅で並ぶ。
/// </summary>
struct UITabEntry {
    std::string label;
    std::string pageUuid;
};

class UITabBarComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UITabBarComponent& c);
    friend void from_json(const nlohmann::json& j, UITabBarComponent& c);

public:
    UITabBarComponent()           = default;
    ~UITabBarComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    // --- 直列化対象 ---
    std::vector<UITabEntry> tabs;
    int activeIndex = 0;
};
