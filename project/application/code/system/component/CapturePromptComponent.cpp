#include "CapturePromptComponent.h"

#include "imgui/imgui.h"

void CapturePromptComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void CapturePromptComponent::Finalize() {}

void CapturePromptComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                                  const std::string& /*_parentLabel*/) {
    // 実行時生成の短命エンティティ。編集ではなく読み取り表示のみ。
    const char* src = (source == 0) ? "Camera" : (source == 1) ? "Screen" : "Mic";
    ImGui::Text("source: %s", src);
    ImGui::TextWrapped("desc: %s", description.c_str());
    ImGui::Text("time: %.2f", time);
}

void to_json(nlohmann::json& j, const CapturePromptComponent& c) {
    j["source"]      = c.source;
    j["description"] = c.description;
    j["time"]        = c.time;
}

void from_json(const nlohmann::json& j, CapturePromptComponent& c) {
    c.source      = j.value("source", 0);
    c.description = j.value("description", std::string());
    c.time        = j.value("time", 0.0);
}
