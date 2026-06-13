#include "VisionPanel.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "VisionAnalyzer.h" // VisionAnalyzer::Frame（マルチフレーム）
#include "FrameHistory.h"

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

        // 動画モードでは選択ソースの直近フレームを時系列に複数枚サンプルして送る。
        const bool videoMode = ctx_->config.visionVideoEnabled;
        const int sampleK    = (ctx_->config.visionVideoFrames < 1) ? 1 : ctx_->config.visionVideoFrames;
        FrameHistory* hist   = (visionSource_ == 0) ? ctx_->cameraHistory : ctx_->screenHistory;
        std::shared_ptr<const CapturedFrame> latest =
            (visionSource_ == 0) ? ctx_->cameraFrame : ctx_->screenFrame;

        std::vector<std::shared_ptr<const CapturedFrame>> samples;
        if (videoMode && hist) {
            samples = hist->Sample(sampleK);
        } else if (latest) {
            samples.push_back(latest);
        }

        std::vector<VisionAnalyzer::Frame> vframes; // AnalyzeAsync が同期コピーするので samples 生存で十分
        for (auto& f : samples) {
            if (!f || f->pixels.empty()) continue;
            vframes.push_back({f->pixels.data(), f->width, f->height});
        }

        std::string prompt = ctx_->config.visionPrompt;
        if (vframes.size() > 1) {
            prompt = "以下は時系列に並んだフレーム（古い順）です。動き・変化・進行に注目してください。\n" + prompt;
        }
        visionAnalyzer_->SetPrompt(prompt);

        if (vframes.size() == 1) {
            isVisionAnalyzing_ = true;
            visionFuture_ = visionAnalyzer_->AnalyzeAsync(vframes[0].bgra, vframes[0].width, vframes[0].height);
        } else if (vframes.size() > 1) {
            isVisionAnalyzing_ = true;
            visionFuture_ = visionAnalyzer_->AnalyzeAsync(vframes);
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
