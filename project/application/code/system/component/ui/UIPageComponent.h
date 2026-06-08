#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <string>

/// <summary>
/// タブで切り替わるページ（コンテナ）。UIElementComponent と同居する。
/// tabBarUuid が指すタブバーの activeIndex が自身の index と一致するときだけ visible になる
/// （UILayoutSystem が毎フレーム設定）。子ウィジェットは UIElementComponent.parentUuid で本ページに従属し、
/// ページ不可視時は effectiveVisible 伝播で連動して隠れる。
/// </summary>
class UIPageComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UIPageComponent& c);
    friend void from_json(const nlohmann::json& j, UIPageComponent& c);

public:
    UIPageComponent()           = default;
    ~UIPageComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    // --- 直列化対象 ---
    std::string tabBarUuid; // 所属タブバーの uuid
    int index = 0;          // タブバー内でのページ番号
};
