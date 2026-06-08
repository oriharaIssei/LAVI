#include "UIElementComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UIElementComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {
    effectiveVisible = visible;
}

void UIElementComponent::Finalize() {}

void UIElementComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                              const std::string& _parentLabel) {
    const std::string label = _parentLabel + "##UIElementComponent";
    ImGui::DragFloat2(("Anchor" + label).c_str(), &anchor[0], 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat2(("Offset" + label).c_str(), &offsetPos[0], 1.0f);
    ImGui::DragFloat2(("Size" + label).c_str(), &size[0], 1.0f, 0.0f, 8192.0f);

    char buf[64] = {};
    std::snprintf(buf, sizeof(buf), "%s", parentUuid.c_str());
    if (ImGui::InputText(("ParentUuid" + label).c_str(), buf, sizeof(buf))) {
        parentUuid = buf;
    }
    ImGui::Checkbox(("Visible" + label).c_str(), &visible);
    ImGui::SameLine();
    ImGui::Checkbox(("Interactive" + label).c_str(), &interactive);
    ImGui::Text("world: (%.0f, %.0f)  hover:%d press:%d", worldPos[0], worldPos[1], hovered, pressed);
}

void to_json(nlohmann::json& j, const UIElementComponent& c) {
    j["anchor"]      = {c.anchor[0], c.anchor[1]};
    j["offsetPos"]   = {c.offsetPos[0], c.offsetPos[1]};
    j["size"]        = {c.size[0], c.size[1]};
    j["parentUuid"]  = c.parentUuid;
    j["visible"]     = c.visible;
    j["interactive"] = c.interactive;
}

void from_json(const nlohmann::json& j, UIElementComponent& c) {
    if (j.contains("anchor")) j["anchor"].get_to(c.anchor);
    if (j.contains("offsetPos")) j["offsetPos"].get_to(c.offsetPos);
    if (j.contains("size")) j["size"].get_to(c.size);
    c.parentUuid  = j.value("parentUuid", std::string());
    c.visible     = j.value("visible", true);
    c.interactive = j.value("interactive", false);
}
