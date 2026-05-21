#include "LLMChatPanel.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "imgui/imgui.h"

void LLMChatPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    llmClient_ = std::make_unique<LLMClient>();
    llmAutoObserveLastTime_ = std::chrono::steady_clock::now();
}

void LLMChatPanel::Finalize() {
    synthPipeline_.Shutdown();
    if (llmClient_) {
        llmClient_->Cancel();
    }
    llmClient_.reset();
}

LLMStreamCallback LLMChatPanel::MakeStreamCallback() {
    return [this](const std::string& delta, bool done) {
        if (!done) {
            std::lock_guard<std::mutex> lock(llmStreamMutex_);
            llmStreamingText_ += delta;
        }
        if (llmSpeakResponse_ && ctx_->voiceVox && ctx_->voiceVox->IsEngineReady()
            && !ctx_->voiceVoxSpeakers.empty()) {
            if (!done) {
                synthPipeline_.FeedDelta(delta);
            } else {
                synthPipeline_.FeedDone();
            }
        }
    };
}

void LLMChatPanel::SendLLMRequest(const std::string& text, const std::vector<LLMClient::ImageFrame>& frames) {
    llmClient_->SetApiKey(ctx_->config.apiKey);
    llmClient_->SetSystemPrompt(ctx_->config.llmSystemPrompt);

    if (frames.empty()) {
        llmClient_->AddMessage("user", text);
    } else {
        llmClient_->AddMessageWithImages("user", text, frames);
    }

    isLLMProcessing_ = true;
    llmStreamingText_.clear();

    if (llmSpeakResponse_ && ctx_->voiceVox && ctx_->voiceVox->IsEngineReady()
        && !ctx_->voiceVoxSpeakers.empty()) {
        int speakerId = ctx_->voiceVoxSpeakers[ctx_->selectedSpeaker].id;
        synthPipeline_.StartSession(ctx_->voiceVox, speakerId);
    }

    llmFuture_ = llmClient_->SendStreamAsync(MakeStreamCallback());
}

void LLMChatPanel::Draw() {
    ImGui::Text("LLM Chat (Claude API)");
    ImGui::Separator();

    ImGui::InputText("API Key##llm", ctx_->config.apiKey.data(), ctx_->config.apiKey.capacity() + 1,
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
    if (ImGui::Button("Save##llm")) {
        SaveAppConfig(ctx_->config);
    }

    ImGui::InputTextMultiline("System Prompt##llm", ctx_->config.llmSystemPrompt.data(),
        ctx_->config.llmSystemPrompt.capacity() + 1, ImVec2(-1, 60),
        ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }, &ctx_->config.llmSystemPrompt);

    ImGui::Spacing();

    ImGui::Text("Text Input:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Text##llm_text", !llmUseWhisper_)) { llmUseWhisper_ = false; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Whisper##llm_whisper", llmUseWhisper_)) { llmUseWhisper_ = true; }
    ImGui::SameLine();
    ImGui::Text("  Attach:");
    ImGui::SameLine();
    ImGui::Checkbox("WebCam##llm", &llmAttachWebCam_);
    ImGui::SameLine();
    ImGui::Checkbox("Screen##llm", &llmAttachScreen_);

    ImGui::Checkbox("VoiceVox Output##llm", &llmSpeakResponse_);
    if (llmSpeakResponse_ && ctx_->voiceVoxSpeakers.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(Start VoiceVox Engine first)");
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Auto Observe##llm")) {
        bool wasAutoObserve = llmAutoObserve_;
        ImGui::Checkbox("Enable##auto_observe", &llmAutoObserve_);
        if (llmAutoObserve_ && !wasAutoObserve) {
            llmAutoObserveLastTime_ = std::chrono::steady_clock::now();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Interval (s)##auto_observe", &llmAutoObserveInterval_, 3.0f, 60.0f, "%.0f");

        ImGui::Checkbox("WebCam##auto_obs", &llmAutoObserveWebCam_);
        ImGui::SameLine();
        ImGui::Checkbox("Screen##auto_obs", &llmAutoObserveScreen_);

        if (llmAutoObserve_) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - llmAutoObserveLastTime_).count();
            float remaining = llmAutoObserveInterval_ - elapsed;
            if (remaining < 0) remaining = 0;
            ImGui::Text("Next observation in: %.1f s", remaining);
        }
    }

    ImGui::Separator();

    float historyHeight = ImGui::GetContentRegionAvail().y - 120.0f;
    if (historyHeight < 100.0f) historyHeight = 100.0f;

    ImGui::BeginChild("ChatHistory", ImVec2(0, historyHeight), true);
    {
        const auto& history = llmClient_->GetHistory();
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& msg = history[i];
            if (msg.role == "user") {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "You:");
                ImGui::SameLine();
                if (!msg.images.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Image x%d]",
                        static_cast<int>(msg.images.size()));
                    ImGui::SameLine();
                }
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LAVI:");
                ImGui::SameLine();
            }
            ImGui::TextWrapped("%s", msg.content.c_str());
            ImGui::Spacing();
        }

        if (isLLMProcessing_) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LAVI:");
            ImGui::SameLine();
            std::lock_guard<std::mutex> lock(llmStreamMutex_);
            ImGui::TextWrapped("%s", llmStreamingText_.empty() ? "..." : llmStreamingText_.c_str());
        }

        if (llmAutoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Check async result
    if (isLLMProcessing_ && llmFuture_.valid() &&
        llmFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        lastLLMResponse_ = llmFuture_.get();
        if (!lastLLMResponse_.success && !lastLLMResponse_.error.empty()) {
            llmClient_->AddMessage("assistant", "[Error] " + lastLLMResponse_.error);
        }
        isLLMProcessing_ = false;
        llmStreamingText_.clear();
    }

    // Stop synth worker when streaming is done
    if (!isLLMProcessing_) {
        synthPipeline_.StopWorker();
    }

    // Play synthesized audio
    synthPipeline_.UpdatePlayback(ctx_->voiceVox, ctx_->isSpeaking, ctx_->speakFuture);

    // Auto-observe
    if (llmAutoObserve_ && !isLLMProcessing_ && !ctx_->config.apiKey.empty()) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - llmAutoObserveLastTime_).count();
        if (elapsed >= llmAutoObserveInterval_) {
            llmAutoObserveLastTime_ = now;

            std::vector<LLMClient::ImageFrame> frames;

            if (llmAutoObserveWebCam_ && ctx_->webCamera && ctx_->webCamera->IsCapturing()) {
                uint32_t fw = 0, fh = 0;
                if (ctx_->webCamera->GetLatestFrame(ctx_->camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                    frames.push_back({ctx_->camFrameBuffer.data(), fw, fh});
                }
            }
            if (llmAutoObserveScreen_ && ctx_->screenCapture && ctx_->screenCapture->IsCapturing()) {
                uint32_t fw = 0, fh = 0;
                if (ctx_->screenCapture->GetLatestFrame(ctx_->screenFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                    frames.push_back({ctx_->screenFrameBuffer.data(), fw, fh});
                }
            }

            if (!frames.empty()) {
                std::string observePrompt =
                    "[自律観察] 今のカメラ/画面の様子を見てください。"
                    "何か気になること、面白いこと、コメントしたいことがあれば自然に話しかけてください。"
                    "特に何もなければ「特になし」とだけ答えてください。";

                SendLLMRequest(observePrompt, frames);
            }
        }
    }

    if (lastLLMResponse_.inputTokens > 0 || lastLLMResponse_.outputTokens > 0) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tokens: in=%d out=%d",
            lastLLMResponse_.inputTokens, lastLLMResponse_.outputTokens);
        if (lastLLMResponse_.cacheReadInputTokens > 0 || lastLLMResponse_.cacheCreationInputTokens > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), " | Cache: read=%d create=%d",
                lastLLMResponse_.cacheReadInputTokens, lastLLMResponse_.cacheCreationInputTokens);
        }
    }

    // User input area
    bool enterPressed = false;
    if (llmUseWhisper_ && !ctx_->transcribedText.empty()) {
        llmUserInput_ = ctx_->transcribedText;
    }

    if (llmUseWhisper_) ImGui::BeginDisabled();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 160);
    if (ImGui::InputText("##llm_input", llmUserInput_.data(), llmUserInput_.capacity() + 1,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            }, &llmUserInput_)) {
        enterPressed = true;
    }
    ImGui::PopItemWidth();
    if (llmUseWhisper_) ImGui::EndDisabled();

    ImGui::SameLine();

    bool canSend = !llmUserInput_.empty() && !ctx_->config.apiKey.empty() && !isLLMProcessing_;
    if (!canSend) ImGui::BeginDisabled();

    if (ImGui::Button("Send##llm") || (enterPressed && canSend)) {
        std::vector<LLMClient::ImageFrame> frames;

        if (llmAttachWebCam_ && ctx_->webCamera && ctx_->webCamera->IsCapturing()) {
            uint32_t fw = 0, fh = 0;
            if (ctx_->webCamera->GetLatestFrame(ctx_->camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                frames.push_back({ctx_->camFrameBuffer.data(), fw, fh});
            }
        }
        if (llmAttachScreen_ && ctx_->screenCapture && ctx_->screenCapture->IsCapturing()) {
            uint32_t fw = 0, fh = 0;
            if (ctx_->screenCapture->GetLatestFrame(ctx_->screenFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                frames.push_back({ctx_->screenFrameBuffer.data(), fw, fh});
            }
        }

        SendLLMRequest(llmUserInput_, frames);
        llmUserInput_.clear();
    }

    if (!canSend) ImGui::EndDisabled();

    ImGui::SameLine();

    if (isLLMProcessing_) {
        if (ImGui::Button("Cancel##llm")) {
            llmClient_->Cancel();
        }
    } else {
        if (ImGui::Button("Clear##llm")) {
            llmClient_->ClearHistory();
        }
    }
}
