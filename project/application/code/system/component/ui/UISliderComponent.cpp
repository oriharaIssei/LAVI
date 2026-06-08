#include "UISliderComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UISliderComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UISliderComponent::Finalize() {}

void UISliderComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                             const std::string& _parentLabel) {
    const std::string l = _parentLabel + "##UISliderComponent";
    ImGui::DragFloat(("Min" + l).c_str(), &minValue, 0.1f);
    ImGui::DragFloat(("Max" + l).c_str(), &maxValue, 0.1f);
    ImGui::SliderFloat(("Value" + l).c_str(), &value, minValue, maxValue);

    char kbuf[128] = {};
    std::snprintf(kbuf, sizeof(kbuf), "%s", bindKey.c_str());
    if (ImGui::InputText(("BindKey" + l).c_str(), kbuf, sizeof(kbuf))) {
        bindKey = kbuf;
    }
    char lbuf[128] = {};
    std::snprintf(lbuf, sizeof(lbuf), "%s", label.c_str());
    if (ImGui::InputText(("Label" + l).c_str(), lbuf, sizeof(lbuf))) {
        label = lbuf;
    }
}

void to_json(nlohmann::json& j, const UISliderComponent& c) {
    j["minValue"] = c.minValue;
    j["maxValue"] = c.maxValue;
    j["value"]    = c.value;
    j["bindKey"]  = c.bindKey;
    j["label"]    = c.label;
}

void from_json(const nlohmann::json& j, UISliderComponent& c) {
    c.minValue = j.value("minValue", 0.0f);
    c.maxValue = j.value("maxValue", 1.0f);
    c.value    = j.value("value", 0.0f);
    c.bindKey  = j.value("bindKey", std::string());
    c.label    = j.value("label", std::string());
}
