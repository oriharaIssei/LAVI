#include "VisionPanel.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "imgui/imgui.h"

void VisionPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    // VisionAnalyzer の所有・公開は VisionSystem(ECS 分解) へ移譲。Draw で ctx から pull する。
}

void VisionPanel::Finalize() {
    ctx_ = nullptr;
}

void VisionPanel::Draw() {
    // 所有は VisionSystem。共有インスタンスを LaviContext(ctx_) から引いて使う（ECS 分解）。
    visionAnalyzer_ = ctx_->visionAnalyzer; // null の可能性あり（下の canAnalyze で個別ガード）

    ImGui::Text("Vision (Claude API)");
    ImGui::Separator();

    ImGui::InputText("API Key", ctx_->config.apiKey.data(), ctx_->config.apiKey.capacity() + 1,
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }, &ctx_->config.apiKey);

    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        SaveAppConfig(ctx_->config);
    }

    ImGui::InputTextMultiline("Prompt", ctx_->config.visionPrompt.data(), ctx_->config.visionPrompt.capacity() + 1,
        ImVec2(-1, 60), ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }, &ctx_->config.visionPrompt);

    ImGui::Spacing();

    ImGui::RadioButton("WebCamera", &visionSource_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("ScreenCapture", &visionSource_, 1);

    ImGui::Spacing();

    if (isVisionAnalyzing_ && visionFuture_.valid() &&
        visionFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        VisionResult res = visionFuture_.get();
        if (res.success) {
            visionResult_ = res.description;
        } else {
            visionResult_ = "[Error] " + res.error;
        }
        isVisionAnalyzing_ = false;
    }

    bool hasSource = (visionSource_ == 0 && ctx_->webCamera && ctx_->webCamera->IsCapturing())
                  || (visionSource_ == 1 && ctx_->screenCapture && ctx_->screenCapture->IsCapturing());
    bool canAnalyze = hasSource && visionAnalyzer_ && !ctx_->config.apiKey.empty() && !isVisionAnalyzing_;

    if (!canAnalyze) ImGui::BeginDisabled();
    if (ImGui::Button("Analyze")) {
        visionAnalyzer_->SetApiKey(ctx_->config.apiKey);
        visionAnalyzer_->SetPrompt(ctx_->config.visionPrompt);

        const uint8_t* data = nullptr;
        uint32_t w = 0, h = 0;

        if (visionSource_ == 0) {
            uint32_t fw = 0, fh = 0;
            if (ctx_->webCamera->GetLatestFrame(ctx_->camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                data = ctx_->camFrameBuffer.data();
                w = fw;
                h = fh;
            }
        } else {
            uint32_t fw = 0, fh = 0;
            if (ctx_->screenCapture->GetLatestFrame(ctx_->screenFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                data = ctx_->screenFrameBuffer.data();
                w = fw;
                h = fh;
            }
        }

        if (data && w > 0 && h > 0) {
            isVisionAnalyzing_ = true;
            visionFuture_ = visionAnalyzer_->AnalyzeAsync(data, w, h);
        }
    }
    if (!canAnalyze) ImGui::EndDisabled();

    if (!hasSource) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Start capture first");
    }

    if (isVisionAnalyzing_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Analyzing...");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Result:");
    ImGui::TextWrapped("%s", visionResult_.empty() ? "(no result)" : visionResult_.c_str());
}
