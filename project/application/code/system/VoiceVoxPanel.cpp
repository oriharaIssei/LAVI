#include "VoiceVoxPanel.h"
#include "SharedMediaContext.h"

#include "imgui/imgui.h"

void VoiceVoxPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    voiceVox_ = std::make_unique<VoiceVoxClient>();
    ctx_->voiceVox = voiceVox_.get();
}

void VoiceVoxPanel::Finalize() {
    if (voiceVox_) {
        voiceVox_->Stop();
        voiceVox_->StopEngine();
    }
    voiceVox_.reset();
    ctx_->voiceVox = nullptr;
}

void VoiceVoxPanel::Draw() {
    ImGui::Text("VoiceVox (Text-to-Speech)");
    ImGui::Separator();

    ImGui::InputText("Engine Path", voiceVoxEnginePath_.data(), voiceVoxEnginePath_.capacity() + 1,
        ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }, &voiceVoxEnginePath_);

    bool engineRunning = voiceVox_->IsEngineRunning();
    bool engineReady = engineRunning && voiceVox_->IsEngineReady();

    if (!engineRunning) {
        if (ImGui::Button("Start Engine")) {
            voiceVox_->StartEngine(voiceVoxEnginePath_);
            if (voiceVox_->IsEngineReady()) {
                ctx_->voiceVoxSpeakers = voiceVox_->GetSpeakers();
            }
        }
        if (!voiceVox_->GetLastErrorMessage().empty()) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error: %s", voiceVox_->GetLastErrorMessage().c_str());
        }
    } else {
        if (ImGui::Button("Stop Engine")) {
            voiceVox_->StopEngine();
            ctx_->voiceVoxSpeakers.clear();
        }
        ImGui::SameLine();
        if (engineReady) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Ready");
        } else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Starting...");
        }
    }

    if (!engineReady) return;

    ImGui::Spacing();
    ImGui::Separator();

    if (ctx_->voiceVoxSpeakers.empty()) {
        if (ImGui::Button("Refresh Speakers")) {
            ctx_->voiceVoxSpeakers = voiceVox_->GetSpeakers();
        }
    } else {
        std::string selectedLabel = ctx_->voiceVoxSpeakers[ctx_->selectedSpeaker].name
            + " (" + ctx_->voiceVoxSpeakers[ctx_->selectedSpeaker].styleName + ")";
        if (ImGui::BeginCombo("Speaker", selectedLabel.c_str())) {
            for (int i = 0; i < static_cast<int>(ctx_->voiceVoxSpeakers.size()); ++i) {
                std::string label = ctx_->voiceVoxSpeakers[i].name + " (" + ctx_->voiceVoxSpeakers[i].styleName + ")";
                if (ImGui::Selectable(label.c_str(), ctx_->selectedSpeaker == i)) {
                    ctx_->selectedSpeaker = i;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();

    ImGui::InputTextMultiline("##voicevox_text", voiceVoxText_.data(), voiceVoxText_.capacity() + 1,
        ImVec2(-1, 80), ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }, &voiceVoxText_);

    ImGui::Spacing();

    bool canSpeak = !voiceVoxText_.empty() && !ctx_->voiceVoxSpeakers.empty() && !ctx_->isSpeaking;
    if (!canSpeak) ImGui::BeginDisabled();
    if (ImGui::Button("Speak")) {
        ctx_->isSpeaking = true;
        int speakerId = ctx_->voiceVoxSpeakers[ctx_->selectedSpeaker].id;
        ctx_->speakFuture = voiceVox_->SpeakAsync(voiceVoxText_, speakerId);
    }
    if (!canSpeak) ImGui::EndDisabled();

    if (ctx_->isSpeaking) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Speaking...");
    }

    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        voiceVox_->Stop();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Whisper -> VoiceVox");
    if (!ctx_->transcribedText.empty()) {
        if (ImGui::Button("Speak Transcription")) {
            if (!ctx_->isSpeaking && !ctx_->voiceVoxSpeakers.empty()) {
                ctx_->isSpeaking = true;
                int speakerId = ctx_->voiceVoxSpeakers[ctx_->selectedSpeaker].id;
                ctx_->speakFuture = voiceVox_->SpeakAsync(ctx_->transcribedText, speakerId);
            }
        }
        ImGui::SameLine();
        ImGui::TextWrapped("%s", ctx_->transcribedText.c_str());
    } else {
        ImGui::TextDisabled("(Transcribe audio first in Microphone tab)");
    }
}
