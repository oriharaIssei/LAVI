#include "UIButtonComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UIButtonComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UIButtonComponent::Finalize() {}

void UIButtonComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                             const std::string& _parentLabel) {
    const std::string label_ = _parentLabel + "##UIButtonComponent";

    char lbuf[128] = {};
    std::snprintf(lbuf, sizeof(lbuf), "%s", label.c_str());
    if (ImGui::InputText(("Label" + label_).c_str(), lbuf, sizeof(lbuf))) {
        label = lbuf;
    }
    char abuf[128] = {};
    std::snprintf(abuf, sizeof(abuf), "%s", actionId.c_str());
    if (ImGui::InputText(("ActionId" + label_).c_str(), abuf, sizeof(abuf))) {
        actionId = abuf;
    }
    ImGui::ColorEdit4(("Color" + label_).c_str(), &color[0]);
    ImGui::ColorEdit4(("HoverColor" + label_).c_str(), &hoverColor[0]);
    ImGui::ColorEdit4(("PressColor" + label_).c_str(), &pressColor[0]);
}

void to_json(nlohmann::json& j, const UIButtonComponent& c) {
    j["label"]      = c.label;
    j["actionId"]   = c.actionId;
    j["color"]      = {c.color[0], c.color[1], c.color[2], c.color[3]};
    j["hoverColor"] = {c.hoverColor[0], c.hoverColor[1], c.hoverColor[2], c.hoverColor[3]};
    j["pressColor"] = {c.pressColor[0], c.pressColor[1], c.pressColor[2], c.pressColor[3]};
}

void from_json(const nlohmann::json& j, UIButtonComponent& c) {
    c.label    = j.value("label", std::string());
    c.actionId = j.value("actionId", std::string());
    if (j.contains("color")) j["color"].get_to(c.color);
    if (j.contains("hoverColor")) j["hoverColor"].get_to(c.hoverColor);
    if (j.contains("pressColor")) j["pressColor"].get_to(c.pressColor);
}
