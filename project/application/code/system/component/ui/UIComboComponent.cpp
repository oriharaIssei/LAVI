#include "UIComboComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UIComboComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UIComboComponent::Finalize() {}

void UIComboComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                            const std::string& _parentLabel) {
    const std::string l = _parentLabel + "##UIComboComponent";
    char rbuf[64] = {};
    std::snprintf(rbuf, sizeof(rbuf), "%s", role.c_str());
    if (ImGui::InputText(("Role" + l).c_str(), rbuf, sizeof(rbuf))) {
        role = rbuf;
    }
    char pbuf[128] = {};
    std::snprintf(pbuf, sizeof(pbuf), "%s", placeholder.c_str());
    if (ImGui::InputText(("Placeholder" + l).c_str(), pbuf, sizeof(pbuf))) {
        placeholder = pbuf;
    }
    ImGui::Text("items=%d selected=%d %s", static_cast<int>(items.size()), selectedIndex,
                expanded ? "(expanded)" : "");
}

void to_json(nlohmann::json& j, const UIComboComponent& c) {
    j["role"]        = c.role;
    j["placeholder"] = c.placeholder;
}

void from_json(const nlohmann::json& j, UIComboComponent& c) {
    c.role        = j.value("role", std::string());
    c.placeholder = j.value("placeholder", std::string());
}
