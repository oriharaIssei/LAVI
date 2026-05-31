#include "LLMChatPanel.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "EmotionTag.h"
#include "MemoryPanel.h"
#include "LocalLLM.h"
#include "ConversationMemory.h"
#include "WebAction.h"
#include "LongTermMemory.h"
#include "SentenceEmbedding.h"
#include "system/action/ActionPipeline.h"

#include "globalVariables/GlobalVariables.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#define ENGINE_PROCESS_MANAGER
#include <EngineInclude.h>

#include "imgui/imgui.h"

void LLMChatPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    llmClient_ = std::make_unique<LLMClient>();
    llmAutoObserveLastTime_ = std::chrono::steady_clock::now();
    LoadPanelState();
}

void LLMChatPanel::SetMemoryPanel(MemoryPanel* memPanel) {
    memoryPanel_ = memPanel;

    if (memPanel && llmClient_) {
        auto* convMem = memPanel->GetConversationMemory();
        if (convMem) {
            auto& entries = convMem->GetRecentEntries();
            for (auto& e : entries) {
                if (!e.hasImage) {
                    llmClient_->AddMessage(e.role, e.content);
                }
            }
        }
    }
}

void LLMChatPanel::Finalize() {
    SavePanelState();

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

void LLMChatPanel::SavePanelState() {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    const std::string sc = "Settings";
    const std::string gr = "LLMChatPanel";
    gv->SetValue(sc, gr, "SpeakResponse", llmSpeakResponse_);
    gv->SetValue(sc, gr, "PlayFiller", llmPlayFiller_);
    gv->SetValue(sc, gr, "UseWhisper", llmUseWhisper_);
    gv->SetValue(sc, gr, "UseLocalLLM", useLocalLLM_);
    gv->SetValue(sc, gr, "LocalEnableThinking", localEnableThinking_);
    gv->SetValue(sc, gr, "AttachAppInfo", llmAttachAppInfo_);
    gv->SetValue(sc, gr, "EnableWebSearch", llmEnableWebSearch_);
    gv->SetValue(sc, gr, "UseMemoryContext", useMemoryContext_);
    gv->SetValue(sc, gr, "AutoObserve", llmAutoObserve_);
    gv->SetValue(sc, gr, "AutoObserveWebCam", llmAutoObserveWebCam_);
    gv->SetValue(sc, gr, "AutoObserveScreen", llmAutoObserveScreen_);
    gv->SetValue(sc, gr, "AutoObserveInterval", llmAutoObserveInterval_);
    gv->SaveFile(sc, gr);
}

void LLMChatPanel::LoadPanelState() {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    const std::string sc = "Settings";
    const std::string gr = "LLMChatPanel";
    gv->LoadFile(sc, gr);
    llmSpeakResponse_ = *gv->AddValue<bool>(sc, gr, "SpeakResponse", llmSpeakResponse_);
    llmPlayFiller_ = *gv->AddValue<bool>(sc, gr, "PlayFiller", llmPlayFiller_);
    llmUseWhisper_ = *gv->AddValue<bool>(sc, gr, "UseWhisper", llmUseWhisper_);
    useLocalLLM_ = *gv->AddValue<bool>(sc, gr, "UseLocalLLM", useLocalLLM_);
    localEnableThinking_ = *gv->AddValue<bool>(sc, gr, "LocalEnableThinking", localEnableThinking_);
    llmAttachAppInfo_ = *gv->AddValue<bool>(sc, gr, "AttachAppInfo", llmAttachAppInfo_);
    llmEnableWebSearch_ = *gv->AddValue<bool>(sc, gr, "EnableWebSearch", llmEnableWebSearch_);
    useMemoryContext_ = *gv->AddValue<bool>(sc, gr, "UseMemoryContext", useMemoryContext_);
    llmAutoObserve_ = *gv->AddValue<bool>(sc, gr, "AutoObserve", llmAutoObserve_);
    llmAutoObserveWebCam_ = *gv->AddValue<bool>(sc, gr, "AutoObserveWebCam", llmAutoObserveWebCam_);
    llmAutoObserveScreen_ = *gv->AddValue<bool>(sc, gr, "AutoObserveScreen", llmAutoObserveScreen_);
    llmAutoObserveInterval_ = *gv->AddValue<float>(sc, gr, "AutoObserveInterval", llmAutoObserveInterval_);
}

LLMStreamCallback LLMChatPanel::MakeStreamCallback() {
    speechTagBuf_.clear();
    inActionTag_ = false;

    return [this](const std::string& delta, bool done) {
        if (!done) {
            std::lock_guard<std::mutex> lock(llmStreamMutex_);
            llmStreamingText_ += delta;
        }
        if (llmSpeakResponse_ && ctx_->voiceVox && ctx_->voiceVox->IsEngineReady()
            && !ctx_->voiceVoxSpeakers.empty()) {
            if (done) {
                if (!speechTagBuf_.empty()) {
                    synthPipeline_.FeedDelta(speechTagBuf_);
                    speechTagBuf_.clear();
                }
                synthPipeline_.FeedDone();
                return;
            }

            speechTagBuf_ += delta;
            static const std::string kTag = "[browser:";
            while (!speechTagBuf_.empty()) {
                if (inActionTag_) {
                    auto end = speechTagBuf_.find(']');
                    if (end == std::string::npos) break;
                    speechTagBuf_ = speechTagBuf_.substr(end + 1);
                    inActionTag_ = false;
                } else {
                    auto tagPos = speechTagBuf_.find(kTag);
                    if (tagPos == std::string::npos) {
                        size_t safe = speechTagBuf_.size();
                        for (size_t n = 1; n < kTag.size() && n <= speechTagBuf_.size(); ++n) {
                            if (speechTagBuf_.compare(speechTagBuf_.size() - n, n, kTag, 0, n) == 0) {
                                safe = speechTagBuf_.size() - n;
                                break;
                            }
                        }
                        if (safe > 0) {
                            synthPipeline_.FeedDelta(speechTagBuf_.substr(0, safe));
                        }
                        speechTagBuf_ = speechTagBuf_.substr(safe);
                        break;
                    }
                    if (tagPos > 0) {
                        synthPipeline_.FeedDelta(speechTagBuf_.substr(0, tagPos));
                    }
                    speechTagBuf_ = speechTagBuf_.substr(tagPos);
                    inActionTag_ = true;
                }
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

static bool NeedsSummary(const std::string& text) {
    static const char* kw[] = {
        "\xE8\xA6\x81\xE7\xB4\x84", // 要約
        "\xE3\x81\xBE\xE3\x81\xA8\xE3\x82\x81", // まとめ
        "\xE8\xAA\xAC\xE6\x98\x8E", // 説明
        "summary",
    };
    for (const auto* w : kw) {
        if (text.find(w) != std::string::npos) return true;
    }
    return false;
}

void LLMChatPanel::SendLLMRequest(const std::string& text, const std::vector<LLMClient::ImageFrame>& frames, bool playFiller) {
    llmClient_->SetApiKey(ctx_->config.apiKey);
    llmClient_->SetEnableWebSearch(llmEnableWebSearch_ && NeedsSummary(text));

    // ペルソナ（固定）と、毎ターン変わる文脈（記憶・Web・アプリ情報）を分けて持つ。
    // クラウドは従来どおり system に全部入れる。ローカルは persona を system に固定し、
    // 変動文脈は最新ユーザー発話に載せる（persona+履歴のKVプレフィックスを再利用＝高速化、かつ記憶は見せる）。
    const std::string personaPrompt = ctx_->config.llmSystemPrompt;
    std::string volatileContext;

    if (useMemoryContext_ && memoryPanel_) {
        std::string memCtx = memoryPanel_->BuildMemoryContext();
        if (!memCtx.empty()) {
            volatileContext += "\n\n" + memCtx;
        }
    }

    bool hasAction = actionPipeline_ ? actionPipeline_->ContainsActionKeyword(text)
                                     : ContainsActionKeyword(text);
    if (llmEnableWebSearch_ || hasAction) {
        volatileContext += "\n\n";
        volatileContext += GetWebActionPrompt();
    }

    if (llmAttachAppInfo_) {
        auto apps = OriGine::ProcessManager::EnumerateWindows();
        std::string appText = OriGine::ProcessManager::FormatAsText(apps);
        if (!appText.empty()) {
            volatileContext += "\n\n## 現在ユーザーが開いているアプリケーション\n" + appText;
        }
    }

    std::string systemPrompt = personaPrompt + volatileContext; // クラウド用（従来互換）
    llmClient_->SetSystemPrompt(systemPrompt);

    if (memoryPanel_) {
        memoryPanel_->GetConversationMemory()->PushMessage("user", text, !frames.empty());
        memoryPanel_->NotifyUserMessage(text);
    }

    std::string userText = text;
    if (!frames.empty() && !llmAttachAppInfo_) {
        auto fg = OriGine::ProcessManager::GetForegroundApp();
        if (!fg.exeName.empty()) {
            std::string appCtx = OriGine::ProcessManager::FormatAsText({fg});
            userText += "\n[現在のフォアグラウンドアプリ: " + appCtx + "]";
        }
    }

    if (frames.empty()) {
        llmClient_->AddMessage("user", userText);
    } else {
        llmClient_->AddMessageWithImages("user", userText, frames);
    }

    // ローカル LLM 使用時、モデル未ロードなら送信しない
    LocalLLM* local = (useLocalLLM_ && memoryPanel_) ? memoryPanel_->GetLocalLLM() : nullptr;
    if (useLocalLLM_ && (!local || !local->IsModelLoaded())) {
        lastLLMResponse_ = LLMResponse{};
        lastLLMResponse_.error = "Local LLM model not loaded";
        return;
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

    if (local) {
        local->SetDisableThinking(!localEnableThinking_);
        // 直近の会話履歴を LocalChatMessage へ（ローカルは文脈長が小さいので末尾のみ）
        const auto& hist = llmClient_->GetHistory();
        std::vector<LocalChatMessage> msgs;
        size_t startIdx = hist.size() > 16 ? hist.size() - 16 : 0;
        for (size_t i = startIdx; i < hist.size(); ++i) {
            msgs.push_back({hist[i].role, hist[i].content});
        }
        // 記憶・アプリ情報などの変動文脈は「今回のユーザー発話」に載せる。
        // → persona(system) と過去履歴は不変のまま＝KVプレフィックス再利用で高速、かつ記憶は参照される。
        if (!volatileContext.empty() && !msgs.empty()) {
            msgs.back().content += "\n\n[参考情報]" + volatileContext;
        }
        // persona のみを system に（固定）。クラウドと同じストリーミングCBを流用（done は完了時）
        activeStreamCb_ = MakeStreamCallback();
        localFuture_ = local->GenerateChatStreamAsync(
            personaPrompt, msgs,
            [this](const std::string& tok) { activeStreamCb_(tok, false); });
    } else {
        llmFuture_ = llmClient_->SendStreamAsync(MakeStreamCallback());
    }
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

    // バックエンド選択（クラウド Claude / ローカル LLM 共有インスタンス）
    ImGui::Text("Backend:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Cloud (Claude)##backend", !useLocalLLM_)) { useLocalLLM_ = false; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Local##backend", useLocalLLM_)) { useLocalLLM_ = true; }
    if (useLocalLLM_) {
        LocalLLM* local = memoryPanel_ ? memoryPanel_->GetLocalLLM() : nullptr;
        bool loaded = local && local->IsModelLoaded();
        ImGui::SameLine();
        if (loaded) {
            ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "(model loaded)");
        } else {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(load model in Memory panel)");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Thinking##local", &localEnableThinking_);
        ImGui::TextDisabled("Local: 画像/画面添付・Web検索は無効。Thinking OFF で高速・<think>除去");
    }

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
    ImGui::SameLine();
    ImGui::Checkbox("Apps##llm", &llmAttachAppInfo_);

    ImGui::Checkbox("Memory##llm", &useMemoryContext_);
    ImGui::SameLine();
    ImGui::Checkbox("Web##llm", &llmEnableWebSearch_);
    ImGui::SameLine();
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
            std::string display = (msg.role == "assistant") ? StripWebActions(StripEmotionTag(msg.content)) : msg.content;
            ImGui::TextWrapped("%s", display.c_str());
            ImGui::Spacing();
        }

        if (isLLMProcessing_) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LAVI:");
            ImGui::SameLine();
            std::string streamDisplay;
            {
                std::lock_guard<std::mutex> lock(llmStreamMutex_);
                streamDisplay = StripWebActions(StripEmotionTag(llmStreamingText_));
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
        } else if (lastLLMResponse_.success) {
            auto actions = ParseWebActions(lastLLMResponse_.content);
            if (!actions.empty()) {
                LongTermMemory* ltm = memoryPanel_ ? memoryPanel_->GetLongTermMemory() : nullptr;
                ExecuteWebActions(actions, ltm);
            }
            if (memoryPanel_) {
                memoryPanel_->GetConversationMemory()->PushMessage("assistant", lastLLMResponse_.content);
            }
        }
        isLLMProcessing_ = false;
        llmStreamingText_.clear();
    }

    // Local LLM async result
    if (isLLMProcessing_ && localFuture_.valid() &&
        localFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::string out = localFuture_.get();
        if (activeStreamCb_) activeStreamCb_("", true); // 合成パイプラインの終端

        lastLLMResponse_ = LLMResponse{};
        lastLLMResponse_.content = out;
        lastLLMResponse_.success = !out.empty();

        if (!out.empty()) {
            llmClient_->AddMessage("assistant", out);
            auto actions = ParseWebActions(out);
            if (!actions.empty()) {
                LongTermMemory* ltm = memoryPanel_ ? memoryPanel_->GetLongTermMemory() : nullptr;
                ExecuteWebActions(actions, ltm);
            }
            if (memoryPanel_) {
                memoryPanel_->GetConversationMemory()->PushMessage("assistant", out);
            }
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

    bool backendReady;
    if (useLocalLLM_) {
        LocalLLM* local = memoryPanel_ ? memoryPanel_->GetLocalLLM() : nullptr;
        backendReady = local && local->IsModelLoaded();
    } else {
        backendReady = !ctx_->config.apiKey.empty();
    }
    bool canSend = !llmUserInput_.empty() && backendReady && !isLLMProcessing_;
    if (!canSend) ImGui::BeginDisabled();

    if (ImGui::Button("Send##llm") || (enterPressed && canSend)) {
        // 発話から興味関心を学習
        if (memoryPanel_) {
            LearnInterestsFromSpeech(llmUserInput_, memoryPanel_->GetLongTermMemory());
        }

        // ActionPipeline: Intent → Capability → Context → Plan → Execute
        if (actionPipeline_ && memoryPanel_) {
            LongTermMemory* ltm = memoryPanel_->GetLongTermMemory();
            auto actionResult = actionPipeline_->Process(llmUserInput_, ltm, embedding_);
            if (actionResult.handled) {
                llmClient_->AddMessage("user", llmUserInput_);
                llmClient_->AddMessage("assistant", actionResult.spokenText);
                if (memoryPanel_) {
                    memoryPanel_->GetConversationMemory()->PushMessage("user", llmUserInput_);
                    memoryPanel_->GetConversationMemory()->PushMessage("assistant", actionResult.spokenText);
                    memoryPanel_->NotifyUserMessage(llmUserInput_);
                }
                if (llmSpeakResponse_ && ctx_->voiceVox && ctx_->voiceVox->IsEngineReady()
                    && !ctx_->voiceVoxSpeakers.empty()) {
                    int speakerId = ctx_->voiceVoxSpeakers[ctx_->selectedSpeaker].id;
                    synthPipeline_.StartSession(ctx_->voiceVox, speakerId);
                    synthPipeline_.FeedDelta(actionResult.spokenText);
                    synthPipeline_.FeedDone();
                }
                llmUserInput_.clear();
                goto done_send;
            }
        }

        {
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
        done_send:;
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
