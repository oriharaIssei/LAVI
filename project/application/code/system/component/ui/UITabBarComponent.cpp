#include "UITabBarComponent.h"

#include "imgui/imgui.h"

void UITabBarComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UITabBarComponent::Finalize() {}

void UITabBarComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                             const std::string& _parentLabel) {
    const std::string l = _parentLabel + "##UITabBarComponent";
    ImGui::Text("tabs: %d  active: %d", static_cast<int>(tabs.size()), activeIndex);
    for (size_t i = 0; i < tabs.size(); ++i) {
        ImGui::Text("[%zu] %s -> %s", i, tabs[i].label.c_str(), tabs[i].pageUuid.c_str());
    }
    ImGui::DragInt(("ActiveIndex" + l).c_str(), &activeIndex, 0.1f, 0, static_cast<int>(tabs.size()) - 1);
}

void to_json(nlohmann::json& j, const UITabBarComponent& c) {
    j["activeIndex"] = c.activeIndex;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& t : c.tabs) {
        arr.push_back({{"label", t.label}, {"pageUuid", t.pageUuid}});
    }
    j["tabs"] = arr;
}

void from_json(const nlohmann::json& j, UITabBarComponent& c) {
    c.activeIndex = j.value("activeIndex", 0);
    c.tabs.clear();
    if (j.contains("tabs")) {
        for (const auto& e : j["tabs"]) {
            UITabEntry t;
            t.label    = e.value("label", std::string());
            t.pageUuid = e.value("pageUuid", std::string());
            c.tabs.push_back(std::move(t));
        }
    }
}
