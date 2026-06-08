#include "UITextInputComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UITextInputComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UITextInputComponent::Finalize() {}

void UITextInputComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                                const std::string& _parentLabel) {
    const std::string l = _parentLabel + "##UITextInputComponent";

    char abuf[128] = {};
    std::snprintf(abuf, sizeof(abuf), "%s", actionId.c_str());
    if (ImGui::InputText(("ActionId" + l).c_str(), abuf, sizeof(abuf))) {
        actionId = abuf;
    }
    char pbuf[128] = {};
    std::snprintf(pbuf, sizeof(pbuf), "%s", placeholder.c_str());
    if (ImGui::InputText(("Placeholder" + l).c_str(), pbuf, sizeof(pbuf))) {
        placeholder = pbuf;
    }
    ImGui::Checkbox(("SubmitClears" + l).c_str(), &submitClears);
    ImGui::DragInt(("MaxLength" + l).c_str(), &maxLength, 1.0f, 1, 8192);
    ImGui::Checkbox(("Password" + l).c_str(), &password);
}

void to_json(nlohmann::json& j, const UITextInputComponent& c) {
    j["actionId"]     = c.actionId;
    j["placeholder"]  = c.placeholder;
    j["submitClears"] = c.submitClears;
    j["maxLength"]    = c.maxLength;
    j["password"]     = c.password;
}

void from_json(const nlohmann::json& j, UITextInputComponent& c) {
    c.actionId     = j.value("actionId", std::string());
    c.placeholder  = j.value("placeholder", std::string());
    c.submitClears = j.value("submitClears", true);
    c.maxLength    = j.value("maxLength", 512);
    c.password     = j.value("password", false);
}
