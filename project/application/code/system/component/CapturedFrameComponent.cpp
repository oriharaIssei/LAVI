#include "CapturedFrameComponent.h"

#include "imgui/imgui.h"

void CapturedFrameComponent::Initialize(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/) {}

void CapturedFrameComponent::Finalize() { frame.reset(); }

void CapturedFrameComponent::Edit(OriGine::Scene* /*_scene*/, OriGine::EntityHandle /*_owner*/,
                                  const std::string& /*_parentLabel*/) {
    const char* src = (source == 0) ? "Camera" : "Screen";
    ImGui::Text("source: %s", src);
    if (frame) {
        ImGui::Text("frame: %ux%u seq=%llu", frame->width, frame->height,
                    static_cast<unsigned long long>(frame->seq));
    } else {
        ImGui::TextUnformatted("frame: (none)");
    }
}

void to_json(nlohmann::json& j, const CapturedFrameComponent& c) {
    j["source"] = c.source;
}

void from_json(const nlohmann::json& j, CapturedFrameComponent& c) {
    c.source = j.value("source", 0);
}
