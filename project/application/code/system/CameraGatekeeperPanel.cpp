#include "CameraGatekeeperPanel.h"
#include "SharedMediaContext.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "imgui/imgui.h"
#include "util/StringUtil.h"

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

void CameraGatekeeperPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    gk_ = std::make_unique<CameraGatekeeper>();
}

void CameraGatekeeperPanel::Finalize() {
    gk_.reset();
}

void CameraGatekeeperPanel::Evaluate() {
    if (!loaded_ || !ctx_->webCamera || !ctx_->webCamera->IsCapturing()) return;

    uint32_t fw = 0, fh = 0;
    if (ctx_->webCamera->GetLatestFrame(ctx_->camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
        lastResult_ = gk_->Evaluate(ctx_->camFrameBuffer.data(), fw, fh);
        if (gk_->ShouldTrigger(lastResult_)) {
            const double now = ImGui::GetTime();
            if (now - lastTriggerTime_ >= cooldownSec_) {  // クールダウン中は抑制
                lastTriggerTime_ = now;
                triggerLog_.push_back(std::string("[trigger] -> ") +
                                      CameraGatekeeper::EmotionName(lastResult_.dominant));
                if (triggerLog_.size() > 20) triggerLog_.erase(triggerLog_.begin());
            }
        }
    }
}

void CameraGatekeeperPanel::Draw() {
    ImGui::Text("Camera Gatekeeper (OpenCV Haar + FER+)");
    ImGui::Separator();

    ImGui::InputText("FER+ Model", ferModelPath_.data(), ferModelPath_.capacity() + 1,
                     ImGuiInputTextFlags_CallbackResize, InputTextResize, &ferModelPath_);
    ImGui::InputText("Haar XML", haarPath_.data(), haarPath_.capacity() + 1,
                     ImGuiInputTextFlags_CallbackResize, InputTextResize, &haarPath_);
    ImGui::Checkbox("Use GPU", &useGpu_);

    if (ImGui::Button(loaded_ ? "Reload Models" : "Load Models")) {
        loaded_ = gk_->Initialize(ConvertString(ferModelPath_), haarPath_, useGpu_);
        status_ = loaded_ ? "Loaded." : ("[Error] " + gk_->LastError());
        gk_->SetConfidenceThreshold(threshold_);
        gk_->SetDetectionParams(scaleFactor_, minNeighbors_, minFaceSize_);
        gk_->SetConfirmFrames(confirmFrames_);
        gk_->SetIgnoreNeutral(ignoreNeutral_);
        gk_->SetNeutralBias(neutralBias_);
        gk_->ResetState();
    }
    ImGui::SameLine();
    ImGui::TextColored(loaded_ ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0.5f, 0, 1),
                       "%s", status_.empty() ? "(not loaded)" : status_.c_str());

    if (!loaded_) return;

    ImGui::Spacing();
    if (ImGui::SliderFloat("Confidence Threshold", &threshold_, 0.0f, 1.0f)) {
        gk_->SetConfidenceThreshold(threshold_);
    }

    if (ImGui::CollapsingHeader("Face Detection")) {
        bool changed = false;
        changed |= ImGui::SliderFloat("scaleFactor", &scaleFactor_, 1.05f, 1.4f);
        changed |= ImGui::SliderInt("minNeighbors", &minNeighbors_, 1, 10);
        changed |= ImGui::SliderInt("minFaceSize (px, 0=none)", &minFaceSize_, 0, 300);
        if (changed) {
            gk_->SetDetectionParams(scaleFactor_, minNeighbors_, minFaceSize_);
        }
    }

    if (ImGui::CollapsingHeader("Trigger Stabilization", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderInt("confirm frames", &confirmFrames_, 1, 15)) {
            gk_->SetConfirmFrames(confirmFrames_);
        }
        if (ImGui::Checkbox("ignore neutral", &ignoreNeutral_)) {
            gk_->SetIgnoreNeutral(ignoreNeutral_);
        }
        if (ImGui::SliderFloat("neutral suppression", &neutralBias_, 0.0f, 0.95f)) {
            gk_->SetNeutralBias(neutralBias_);
        }
        ImGui::SliderFloat("cooldown (sec)", &cooldownSec_, 0.0f, 5.0f);
    }

    ImGui::Spacing();
    ImGui::Checkbox("Auto Monitor", &autoMonitor_);
    ImGui::SameLine();
    if (ImGui::Button("Evaluate Once")) {
        Evaluate();
    }

    if (autoMonitor_) {
        const double now = ImGui::GetTime();
        if (now - lastEvalTime_ >= 0.3) {  // ~3fps に間引き
            lastEvalTime_ = now;
            Evaluate();
        }
    }

    bool hasCam = ctx_->webCamera && ctx_->webCamera->IsCapturing();
    if (!hasCam) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Start WebCamera capture first");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Face: %s", lastResult_.faceDetected ? "detected" : "none");
    if (lastResult_.faceDetected) {
        ImGui::Text("Dominant: %s (%.0f%%)",
                    CameraGatekeeper::EmotionName(lastResult_.dominant),
                    lastResult_.confidence * 100.0f);
        for (int i = 0; i < 8; ++i) {
            ImGui::ProgressBar(lastResult_.scores[i], ImVec2(-1, 0),
                               CameraGatekeeper::EmotionName(static_cast<Emotion>(i)));
        }
    }

    ImGui::Spacing();
    ImGui::Text("Triggers (emotion change):");
    ImGui::BeginChild("triggers", ImVec2(0, 120), true);
    for (const auto& t : triggerLog_) {
        ImGui::TextUnformatted(t.c_str());
    }
    ImGui::EndChild();
}
