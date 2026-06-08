#include "UIPageComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UIPageComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UIPageComponent::Finalize() {}

void UIPageComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                           const std::string& _parentLabel) {
    const std::string l = _parentLabel + "##UIPageComponent";
    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "%s", tabBarUuid.c_str());
    if (ImGui::InputText(("TabBarUuid" + l).c_str(), buf, sizeof(buf))) {
        tabBarUuid = buf;
    }
    ImGui::DragInt(("Index" + l).c_str(), &index, 0.1f, 0, 64);
}

void to_json(nlohmann::json& j, const UIPageComponent& c) {
    j["tabBarUuid"] = c.tabBarUuid;
    j["index"]      = c.index;
}

void from_json(const nlohmann::json& j, UIPageComponent& c) {
    c.tabBarUuid = j.value("tabBarUuid", std::string());
    c.index      = j.value("index", 0);
}
