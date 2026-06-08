#include "UIChatLogComponent.h"

#include "imgui/imgui.h"

#include <cstdio>

void UIChatLogComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void UIChatLogComponent::Finalize() {}

void UIChatLogComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                              const std::string& _parentLabel) {
    const std::string l = _parentLabel + "##UIChatLogComponent";
    ImGui::DragInt(("MaxTurns" + l).c_str(), &maxTurns, 1.0f, 1, 1000);

    char ubuf[64] = {};
    std::snprintf(ubuf, sizeof(ubuf), "%s", userPrefix.c_str());
    if (ImGui::InputText(("UserPrefix" + l).c_str(), ubuf, sizeof(ubuf))) {
        userPrefix = ubuf;
    }
    char abuf[64] = {};
    std::snprintf(abuf, sizeof(abuf), "%s", assistantPrefix.c_str());
    if (ImGui::InputText(("AssistantPrefix" + l).c_str(), abuf, sizeof(abuf))) {
        assistantPrefix = abuf;
    }
}

void to_json(nlohmann::json& j, const UIChatLogComponent& c) {
    j["maxTurns"]        = c.maxTurns;
    j["userPrefix"]      = c.userPrefix;
    j["assistantPrefix"] = c.assistantPrefix;
}

void from_json(const nlohmann::json& j, UIChatLogComponent& c) {
    c.maxTurns        = j.value("maxTurns", 200);
    c.userPrefix      = j.value("userPrefix", std::string("あなた: "));
    c.assistantPrefix = j.value("assistantPrefix", std::string("LAVI: "));
}
