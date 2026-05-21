#include "MicGatekeeperPanel.h"
#include "SharedMediaContext.h"

#include "imgui/imgui.h"

namespace {
int InputTextResize(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(data->BufTextLen);
        data->Buf = str->data();
    }
    return 0;
}
}  // namespace

void MicGatekeeperPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    gk_ = std::make_unique<MicGatekeeper>();
    ApplyKeywords();
}

void MicGatekeeperPanel::Finalize() {
    gk_.reset();
}

void MicGatekeeperPanel::ApplyKeywords() {
    std::vector<std::string> kws;
    std::string line;
    for (char c : keywordText_) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) kws.push_back(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) kws.push_back(line);

    gk_->SetKeywords(kws);
    gk_->SetCaseSensitive(caseSensitive_);
}

void MicGatekeeperPanel::Evaluate() {
    lastResult_ = gk_->Evaluate(ctx_->transcribedText);
    if (lastResult_.triggered) {
        std::string msg = "[trigger]";
        for (size_t i = 0; i < lastResult_.matched.size(); ++i) {
            msg += (i ? "," : " ") + lastResult_.matched[i];
        }
        triggerLog_.push_back(msg);
        if (triggerLog_.size() > 20) triggerLog_.erase(triggerLog_.begin());
    }
}

void MicGatekeeperPanel::Draw() {
    ImGui::Text("Mic Gatekeeper (Whisper transcript + keyword match)");
    ImGui::Separator();

    ImGui::TextUnformatted("Keywords (1 per line):");
    if (ImGui::InputTextMultiline("##keywords", keywordText_.data(), keywordText_.capacity() + 1,
                                  ImVec2(-1, 90), ImGuiInputTextFlags_CallbackResize,
                                  InputTextResize, &keywordText_)) {
        ApplyKeywords();
    }
    if (ImGui::Checkbox("Case sensitive (ASCII)", &caseSensitive_)) {
        ApplyKeywords();
    }
    ImGui::SameLine();
    ImGui::Text("(%d keywords)", static_cast<int>(gk_->Keywords().size()));

    ImGui::Spacing();
    ImGui::Checkbox("Auto Monitor", &autoMonitor_);
    ImGui::SameLine();
    if (ImGui::Button("Evaluate Once")) {
        Evaluate();
    }
    if (autoMonitor_) {
        Evaluate();  // テキスト未変化なら内部で短絡するので毎フレーム呼んで良い
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Latest transcript:");
    ImGui::TextWrapped("%s", ctx_->transcribedText.empty() ? "(none)" : ctx_->transcribedText.c_str());

    ImGui::Spacing();
    ImGui::Text("Triggers:");
    ImGui::BeginChild("mic_triggers", ImVec2(0, 120), true);
    for (const auto& t : triggerLog_) {
        ImGui::TextUnformatted(t.c_str());
    }
    ImGui::EndChild();
}
