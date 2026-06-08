#include "UICheckboxComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UICheckboxComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UICheckboxComponent::Finalize() {}

void UICheckboxComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                               const std::string& _parentLabel) {
    const std::string l = _parentLabel + "##UICheckboxComponent";
    char lbuf[128] = {};
    std::snprintf(lbuf, sizeof(lbuf), "%s", label.c_str());
    if (ImGui::InputText(("Label" + l).c_str(), lbuf, sizeof(lbuf))) {
        label = lbuf;
    }
    char rbuf[64] = {};
    std::snprintf(rbuf, sizeof(rbuf), "%s", role.c_str());
    if (ImGui::InputText(("Role" + l).c_str(), rbuf, sizeof(rbuf))) {
        role = rbuf;
    }
    ImGui::Text("checked=%s", checked ? "true" : "false");
}

void to_json(nlohmann::json& j, const UICheckboxComponent& c) {
    j["label"] = c.label;
    j["role"]  = c.role;
}

void from_json(const nlohmann::json& j, UICheckboxComponent& c) {
    c.label = j.value("label", std::string());
    c.role  = j.value("role", std::string());
}
