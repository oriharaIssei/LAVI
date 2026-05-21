#include "LLMChatPanel.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "EmotionTag.h"

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
    if (llmClient_) {
        llmClient_->Cancel();
    }
    if (llmFuture_.valid()) {
        llmFuture_.wait();
    }

    synthPipeline_.Shutdown();

    if (personaClient_) {
        personaClient_->Cancel();
    }
    if (personaFuture_.valid()) {
        personaFuture_.wait();
    }
    personaClient_.reset();
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

static const char* kPersonaMetaPrompt =
    "以下のペルソナ説明から、AIアシスタント「LAVI」用のシステムプロンプトを生成してください。\n"
    "プロンプト本文のみ出力し、それ以外の説明は一切不要です。\n"
    "\n"
    "## 重要な前提\n"
    "- ペルソナ説明は「LAVI自身がどんなキャラクターか」を表す。\n"
    "  例: 「弟」→ LAVIがユーザーの弟として振る舞う。\n"
    "  例: 「お姉さん」→ LAVIがユーザーのお姉さんとして振る舞う。\n"
    "- ユーザーはLAVIから見た相対的な関係になる。\n"
    "  例: LAVIが「弟」→ ユーザーは「お兄ちゃん/お姉ちゃん」になる。\n"
    "  例: LAVIが「メイド」→ ユーザーは「ご主人様」になる。\n"
    "\n"
    "## 一人称・口調の推論ガイド\n"
    "キャラの性別・年齢・立場から自然な一人称を選ぶこと:\n"
    "- 男性的キャラ(弟、少年、男友達等): 「僕」「オレ」「俺」など。「私」は不自然。\n"
    "- 女性的キャラ(姉、妹、彼女等): 「私」「あたし」「うち」など。\n"
    "- フォーマルなキャラ(執事、メイド等): 「私(わたくし)」など。\n"
    "- 子供っぽいキャラ: 「ぼく」「あたし」、自分の名前で呼ぶなど。\n"
    "口調も同様に、立場に合った敬語レベル・カジュアルさを推論すること。\n"
    "- 年下キャラがユーザー(年上)に話す: タメ口寄り、甘え口調もOK。\n"
    "- 年上キャラがユーザー(年下)に話す: 包容力ある口調、諭すような表現。\n"
    "- 対等な関係: フランクな友達口調。\n"
    "\n"
    "## 生成ルール\n"
    "- キャラクターの名前は「LAVI」固定\n"
    "- 上記ガイドに従い一人称・ユーザーの呼び方を推論\n"
    "- 口調・語尾の癖を具体的な台詞例3つ以上で示す\n"
    "- 性格の長所と短所を推測\n"
    "- 好きなもの・嫌いなものを2〜3個ずつ推測\n"
    "- キャラに合ったフィラー(えっと、んー、あー等)を指定\n"
    "- 短い文で区切り、読点で息継ぎリズムを作る話し方ルールを含める\n"
    "- 以下の感情タグ定義を必ず含める:\n"
    "  [joy] - 嬉しい・楽しい\n"
    "  [sadness] - 悲しい・残念\n"
    "  [surprise] - 驚き・意外\n"
    "  [anger] - 怒り・不満\n"
    "  [calm] - 平常・穏やか\n"
    "  [thinking] - 考え中・迷い\n"
    "- 感情タグ付きの出力例を6つ(各タグ1つずつ)含める\n"
    "\n"
    "## ペルソナ説明\n";

void LLMChatPanel::GeneratePersonaPrompt() {
    personaClient_ = std::make_unique<LLMClient>();
    personaClient_->SetApiKey(ctx_->config.apiKey);
    personaClient_->SetModel("claude-haiku-4-5-20251001");
    personaClient_->SetMaxTokens(2048);
    personaClient_->AddMessage("user", std::string(kPersonaMetaPrompt) + personaInput_);

    isGeneratingPersona_ = true;
    personaFuture_ = personaClient_->SendAsync();
}

void LLMChatPanel::SendLLMRequest(const std::string& text, const std::vector<LLMClient::ImageFrame>& frames, bool playFiller) {
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
        if (playFiller) {
            synthPipeline_.QueueFiller();
        }
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

    if (ImGui::CollapsingHeader("Persona Generator##llm")) {
        ImGui::TextWrapped("Persona Description:");
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 110);
        ImGui::InputText("##persona_input", personaInput_.data(), personaInput_.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            }, &personaInput_);
        ImGui::PopItemWidth();
        ImGui::SameLine();

        bool canGenerate = !personaInput_.empty() && !ctx_->config.apiKey.empty() && !isGeneratingPersona_;
        if (!canGenerate) ImGui::BeginDisabled();
        if (ImGui::Button("Generate##persona")) {
            GeneratePersonaPrompt();
        }
        if (!canGenerate) ImGui::EndDisabled();

        if (isGeneratingPersona_) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Generating...");
        }

        if (isGeneratingPersona_ && personaFuture_.valid() &&
            personaFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            LLMResponse res = personaFuture_.get();
            if (res.success && !res.content.empty()) {
                ctx_->config.llmSystemPrompt = res.content;
                SaveAppConfig(ctx_->config);
                llmClient_->ClearHistory();
            }
            isGeneratingPersona_ = false;
            personaClient_.reset();
        }
    }

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
    if (llmSpeakResponse_) {
        ImGui::SameLine();
        ImGui::Checkbox("Filler##llm", &llmPlayFiller_);
        if (ctx_->voiceVoxSpeakers.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(Start VoiceVox Engine first)");
        }
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
            std::string display = (msg.role == "assistant") ? StripEmotionTag(msg.content) : msg.content;
            ImGui::TextWrapped("%s", display.c_str());
            ImGui::Spacing();
        }

        if (isLLMProcessing_) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LAVI:");
            ImGui::SameLine();
            std::string streamDisplay;
            {
                std::lock_guard<std::mutex> lock(llmStreamMutex_);
                streamDisplay = StripEmotionTag(llmStreamingText_);
            }
            ImGui::TextWrapped("%s", streamDisplay.empty() ? "..." : streamDisplay.c_str());
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

        SendLLMRequest(llmUserInput_, frames, llmPlayFiller_);
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
