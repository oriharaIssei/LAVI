#include "MemoryPanel.h"
#include "LocalLLM.h"
#include "ConversationMemory.h"
#include "LongTermMemory.h"
#include "BrowsingHistoryCollector.h"
#include "UserIdentifier.h"
#include "AppUsageTracker.h"
#include "GatekeeperManager.h"
#include "SharedMediaContext.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <filesystem>
#include <sstream>

MemoryPanel::MemoryPanel() = default;
MemoryPanel::~MemoryPanel() = default;

void MemoryPanel::Initialize(SharedMediaContext* ctx, GatekeeperManager* gkManager) {
    ctx_ = ctx;
    gkManager_ = gkManager;

    localLLM_ = std::make_unique<LocalLLM>();
    conversationMemory_ = std::make_unique<ConversationMemory>();
    longTermMemory_ = std::make_unique<LongTermMemory>();

    browsingCollector_ = std::make_unique<BrowsingHistoryCollector>();
    userIdentifier_ = std::make_unique<UserIdentifier>();
    appTracker_ = std::make_unique<AppUsageTracker>();

    conversationMemory_->Initialize(localLLM_.get());

    longTermMemory_->SetStoragePath("application/resource/memory");
    longTermMemory_->Load();

    localModelPath_ = "application/resource/llm/models/gemma-2-2b-jpn-it-Q4_K_M.gguf";
    arcfaceModelPath_ = "application/resource/gatekeeper/arcfaceresnet100.onnx";

    // ArcFace があれば自動ロード
    if (std::filesystem::exists(arcfaceModelPath_)) {
        std::wstring wpath(arcfaceModelPath_.begin(), arcfaceModelPath_.end());
        userIdentifier_->InitializeFace(wpath, true);
    }

    userIdentifier_->Load("application/resource/memory");

    appTracker_->Initialize(longTermMemory_.get(), localLLM_.get(), ctx_->config.apiKey);
}

void MemoryPanel::Finalize() {
    if (factExtractionFuture_.valid()) {
        factExtractionFuture_.wait();
    }
    if (browsingFuture_.valid()) {
        browsingFuture_.wait();
    }
    if (appTracker_) {
        appTracker_->Finalize();
    }
    if (localLLM_) {
        localLLM_->Cancel();
    }
    longTermMemory_->Save();

    userIdentifier_->Save("application/resource/memory");
    appTracker_.reset();
    userIdentifier_.reset();
    browsingCollector_.reset();
    conversationMemory_.reset();
    longTermMemory_.reset();
    localLLM_.reset();
}

void MemoryPanel::Update() {
    conversationMemory_->Update();
    appTracker_->Update(1.0f / 60.0f);

    // 常時リスニング → 会話メモリ自動投入
    if (!ctx_->transcribedText.empty() && ctx_->transcribedText != lastTranscribedText_) {
        lastTranscribedText_ = ctx_->transcribedText;
        conversationMemory_->PushMessage("user", lastTranscribedText_);
        lastUserMessage_ = lastTranscribedText_;
        messagesSinceLastExtraction_++;
    }

    // 自動事実抽出
    PollFactExtraction();
    if (!factExtractionActive_ && messagesSinceLastExtraction_ >= factExtractionThreshold_) {
        TriggerFactExtraction();
    }

    autoSaveTimer_ += 1.0f / 60.0f;
    if (autoSaveTimer_ >= autoSaveInterval_) {
        autoSaveTimer_ = 0.0f;
        longTermMemory_->Save();
    }

    // ブラウザ履歴からの趣味嗜好収集 (非同期)
    if (browsingFuture_.valid() &&
        browsingFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        browsingFuture_.get();
        longTermMemory_->Save();
    }

    if (browsingCollectRequested_ && !browsingCollector_->IsCollecting() &&
        localLLM_->IsModelLoaded() && !localLLM_->IsProcessing()) {
        browsingCollectRequested_ = false;
        browsingFuture_ = std::async(std::launch::async, [this]() {
            browsingCollector_->CollectInterests(longTermMemory_.get(), localLLM_.get());
        });
    }

    if (serviceKeywordRequested_ && !browsingCollector_->IsCollecting() &&
        localLLM_->IsModelLoaded() && !localLLM_->IsProcessing() &&
        (!browsingFuture_.valid() || browsingFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)) {
        serviceKeywordRequested_ = false;
        browsingFuture_ = std::async(std::launch::async, [this]() {
            browsingCollector_->CollectServiceKeywords(longTermMemory_.get(), localLLM_.get(), 30);
        });
    }

    // リアルタイム顔識別
    identifyTimer_ += 1.0f / 60.0f;
    if (identifyTimer_ >= identifyInterval_ && gkManager_ &&
        !userIdentifier_->FaceProfiles().empty()) {
        identifyTimer_ = 0.0f;
        auto& camResult = gkManager_->CameraResult();
        if (camResult.faceDetected && camResult.faceW > 0 && !ctx_->camFrameBuffer.empty()) {
            auto embedding = userIdentifier_->ExtractFaceEmbedding(
                ctx_->camFrameBuffer.data(),
                gkManager_->CameraFrameWidth(),
                gkManager_->CameraFrameHeight(),
                camResult.faceX, camResult.faceY, camResult.faceW, camResult.faceH);
            if (!embedding.empty()) {
                auto result = userIdentifier_->IdentifyFace(embedding);
                ctx_->userIdentified = result.identified;
                ctx_->identifiedUserName = result.identified ? result.name : "";
                ctx_->identifiedSimilarity = result.similarity;

                if (result.identified) {
                    userIdentifier_->UpdateFaceEmbedding(result.profileIndex, embedding, 0.05f);
                }
            }
        } else {
            ctx_->userIdentified = false;
            ctx_->identifiedUserName.clear();
            ctx_->identifiedSimilarity = 0.0f;
        }
    }

    browsingCollectTimer_ += 1.0f / 60.0f;
    if (browsingCollectTimer_ >= browsingCollectInterval_) {
        browsingCollectTimer_ = 0.0f;
        if (localLLM_->IsModelLoaded() && !browsingCollector_->IsCollecting()) {
            browsingCollectRequested_ = true;
        }
    }
}

void MemoryPanel::Draw() {
    ImGui::Text("Memory System");
    ImGui::Separator();

    // Local LLM Status
    if (ImGui::CollapsingHeader("Local LLM (Summarizer)", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool loaded = localLLM_->IsModelLoaded();
        ImGui::Text("Status: %s", loaded ? "Loaded" : "Not Loaded");

        ImGui::InputText("Model Path##mem", localModelPath_.data(), localModelPath_.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            }, &localModelPath_);

        if (!loaded) {
            bool fileExists = std::filesystem::exists(localModelPath_);
            if (!fileExists) ImGui::BeginDisabled();
            if (ImGui::Button("Load Model##mem")) {
                modelLoadRequested_ = true;
            }
            if (!fileExists) {
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(File not found)");
            }

            if (modelLoadRequested_ && !localLLM_->IsProcessing()) {
                localLLM_->LoadModel(localModelPath_);
                modelLoadRequested_ = false;
            }
        } else {
            if (ImGui::Button("Unload##mem")) {
                localLLM_->UnloadModel();
            }
            if (localLLM_->IsProcessing()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Processing...");
            }
        }
    }

    ImGui::Spacing();

    // Short-term Memory
    if (ImGui::CollapsingHeader("Short-term Memory", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto stats = conversationMemory_->GetStats();
        ImGui::Text("Total messages: %d", stats.totalMessages);
        ImGui::Text("Recent (in buffer): %d", stats.recentMessages);
        ImGui::Text("Summarized: %d", stats.summarizedMessages);
        ImGui::Text("Est. tokens: %d", stats.estimatedTokens);

        if (conversationMemory_->IsSummarizing()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Summarizing...");
        }

        std::string summary = conversationMemory_->GetSummary();
        if (!summary.empty()) {
            ImGui::Separator();
            ImGui::Text("Summary:");
            ImGui::TextWrapped("%s", summary.c_str());
        }

        int maxRecent = conversationMemory_->GetStats().recentMessages;
        (void)maxRecent;
        static int maxRecentSetting = 20;
        if (ImGui::SliderInt("Max Recent##mem", &maxRecentSetting, 5, 50)) {
            conversationMemory_->SetMaxRecentEntries(maxRecentSetting);
        }

        if (ImGui::Button("Clear Memory##short")) {
            conversationMemory_->Clear();
        }
    }

    ImGui::Spacing();

    // Long-term Memory
    if (ImGui::CollapsingHeader("Long-term Memory")) {
        auto& profile = longTermMemory_->GetUserProfile();

        ImGui::Text("User Profile:");
        ImGui::InputText("Name##ltm", profile.name.data(), profile.name.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            }, &profile.name);

        ImGui::Separator();

        // Active apps (real-time)
        auto activeApps = appTracker_->GetActiveApps();
        if (!activeApps.empty()) {
            ImGui::Text("Active Apps (%d):", static_cast<int>(activeApps.size()));
            for (auto& a : activeApps) {
                if (a.isForeground) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "  [FG] %s (%.1f min)",
                        a.processName.c_str(), a.sessionMinutes);
                } else {
                    ImGui::Text("  [BG] %s (%.1f min)", a.processName.c_str(), a.sessionMinutes);
                }
            }
            ImGui::Separator();
        }

        if (appTracker_->IsTagging()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Tagging new app (Local LLM / Claude + Web Search)...");
        }

        ImGui::Text("App Usage (top 10):");
        auto& records = longTermMemory_->GetAppUsageRecords();
        std::vector<const AppUsageRecord*> sorted;
        for (auto& r : records) sorted.push_back(&r);
        std::sort(sorted.begin(), sorted.end(),
            [](const AppUsageRecord* a, const AppUsageRecord* b) {
                return (a->totalForegroundMinutes + a->totalBackgroundMinutes) >
                       (b->totalForegroundMinutes + b->totalBackgroundMinutes);
            });
        int shown = 0;
        for (auto* r : sorted) {
            if (shown >= 10) break;
            int fgMin = static_cast<int>(r->totalForegroundMinutes);
            int bgMin = static_cast<int>(r->totalBackgroundMinutes);
            std::string tagStr;
            for (size_t i = 0; i < r->tags.size(); ++i) {
                if (i > 0) tagStr += ", ";
                tagStr += r->tags[i];
            }
            if (tagStr.empty()) {
                ImGui::BulletText("%s - FG %dmin / BG %dmin, %d times",
                    r->processName.c_str(), fgMin, bgMin, r->launchCount);
            } else {
                ImGui::BulletText("%s [%s] - FG %dmin / BG %dmin, %d times",
                    r->processName.c_str(), tagStr.c_str(), fgMin, bgMin, r->launchCount);
            }
            ++shown;
        }

        // App transitions (SQLite)
        auto transitions = longTermMemory_->GetAppTransitions(5);
        if (!transitions.empty()) {
            ImGui::Separator();
            ImGui::Text("App Transitions (top 5):");
            for (auto& t : transitions) {
                ImGui::BulletText("%s -> %s (%d times)", t.fromApp.c_str(), t.toApp.c_str(), t.count);
            }
        }

        // Web services
        auto& webServices = longTermMemory_->GetWebServiceRecords();
        if (!webServices.empty()) {
            ImGui::Separator();
            ImGui::Text("Web Services (top 10):");
            std::vector<const WebServiceRecord*> wsSorted;
            for (auto& ws : webServices) wsSorted.push_back(&ws);
            std::sort(wsSorted.begin(), wsSorted.end(),
                [](const WebServiceRecord* a, const WebServiceRecord* b) {
                    return a->totalMinutes > b->totalMinutes;
                });
            int wsShown = 0;
            for (auto* ws : wsSorted) {
                if (wsShown >= 10) break;
                int min = static_cast<int>(ws->totalMinutes);
                std::string tagStr;
                for (size_t i = 0; i < ws->tags.size(); ++i) {
                    if (i > 0) tagStr += ", ";
                    tagStr += ws->tags[i];
                }
                if (tagStr.empty()) {
                    ImGui::BulletText("%s - %dmin, %d visits",
                        ws->serviceName.c_str(), min, ws->visitCount);
                } else {
                    ImGui::BulletText("%s [%s] - %dmin, %d visits",
                        ws->serviceName.c_str(), tagStr.c_str(), min, ws->visitCount);
                }
                ++wsShown;
            }
        }

        // App launch origins (SQLite)
        auto launches = longTermMemory_->GetAppLaunchRecords(5);
        if (!launches.empty()) {
            ImGui::Separator();
            ImGui::Text("App Launch Origins (top 5):");
            for (auto& l : launches) {
                ImGui::BulletText("%s -> %s (%d times)", l.launchedFrom.c_str(), l.launchedApp.c_str(), l.count);
            }
        }

        ImGui::Separator();
        ImGui::Text("Interests:");
        auto& interests = longTermMemory_->GetUserProfile().interests;
        if (interests.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(none)");
        } else {
            for (size_t i = 0; i < interests.size(); ++i) {
                ImGui::BulletText("%s", interests[i].c_str());
            }
        }

        ImGui::Separator();
        ImGui::Text("Facts: %d stored", longTermMemory_->GetFactCount());

        ImGui::Separator();
        ImGui::Text("Browsing History Collector:");
        if (browsingCollector_->IsCollecting()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Collecting...");
        } else {
            bool canCollect = localLLM_->IsModelLoaded() && !localLLM_->IsProcessing();
            if (!canCollect) ImGui::BeginDisabled();
            if (ImGui::Button("Collect from Browser##browse")) {
                browsingCollectRequested_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Service Keywords##browse")) {
                serviceKeywordRequested_ = true;
            }
            if (!canCollect) ImGui::EndDisabled();

            if (browsingCollector_->LastCollectedCount() > 0) {
                ImGui::SameLine();
                ImGui::Text("Last: +%d", browsingCollector_->LastCollectedCount());
            }
            if (!browsingCollector_->LastError().empty()) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s",
                    browsingCollector_->LastError().c_str());
            }
        }

        ImGui::SliderFloat("Collect Interval (s)##browse", &browsingCollectInterval_, 600.0f, 7200.0f, "%.0f");

        if (ImGui::Button("Save##ltm")) {
            longTermMemory_->Save();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload##ltm")) {
            longTermMemory_->Load();
        }
    }

    ImGui::Spacing();

    // User Identification
    if (ImGui::CollapsingHeader("User Identification")) {
        if (userIdentifier_->IsFaceModelLoaded()) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Face Recognition: ArcFace (Active)");
        } else {
            ImGui::Text("Face Recognition: LBP Fallback");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "(Run setup_face_model.bat for ArcFace)");
        }

        ImGui::Separator();

        // Registration
        ImGui::InputText("Name##register", registerNameBuf_, sizeof(registerNameBuf_));
        bool canRegister = registerNameBuf_[0] != '\0';

        bool hasCamFrame = ctx_->webCamera && !ctx_->camFrameBuffer.empty();
        if (!canRegister || !hasCamFrame) ImGui::BeginDisabled();
        if (ImGui::Button("Register Face##uid")) {
            auto embedding = userIdentifier_->ExtractFaceEmbedding(
                ctx_->camFrameBuffer.data(), 640, 480, 0, 0, 640, 480);
            if (!embedding.empty()) {
                userIdentifier_->RegisterFace(registerNameBuf_, embedding);
                userIdentifier_->Save("application/resource/memory");
            }
        }
        if (!canRegister || !hasCamFrame) ImGui::EndDisabled();

        ImGui::SameLine();

        bool hasVoice = ctx_->voiceSnapshotReady && !ctx_->voiceSnapshotBuffer.empty();
        if (!canRegister || !hasVoice) ImGui::BeginDisabled();
        if (ImGui::Button("Register Voice##uid")) {
            auto mfcc = UserIdentifier::ExtractVoiceFeatures(
                ctx_->voiceSnapshotBuffer.data(),
                static_cast<uint32_t>(ctx_->voiceSnapshotBuffer.size()),
                ctx_->voiceSnapshotSampleRate);
            if (!mfcc.empty()) {
                userIdentifier_->RegisterVoice(registerNameBuf_, mfcc);
                userIdentifier_->Save("application/resource/memory");
            }
            ctx_->voiceSnapshotReady = false;
        }
        if (!canRegister || !hasVoice) {
            ImGui::EndDisabled();
            if (!hasVoice && canRegister) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "(Microphone tab -> Voice Snapshot first)");
            }
        }

        ImGui::Separator();

        // Registered profiles
        auto& faces = userIdentifier_->FaceProfiles();
        // リアルタイム識別結果
        ImGui::Separator();
        ImGui::Text("Live Identification:");
        if (ctx_->userIdentified) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f),
                "Detected: %s (%.1f%%)", ctx_->identifiedUserName.c_str(),
                ctx_->identifiedSimilarity * 100.0f);
        } else if (gkManager_ && gkManager_->CameraResult().faceDetected) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Unknown face (%.1f%%)",
                ctx_->identifiedSimilarity * 100.0f);
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No face detected");
        }

        ImGui::Separator();
        ImGui::Text("Face Profiles: %d", static_cast<int>(faces.size()));
        for (auto& f : faces) {
            ImGui::BulletText("%s (seen %d times, last: %s)",
                f.name.c_str(), f.sightCount, f.lastSeen.c_str());
        }

        auto& voices = userIdentifier_->VoiceProfiles();
        ImGui::Text("Voice Profiles: %d", static_cast<int>(voices.size()));
        for (auto& v : voices) {
            ImGui::BulletText("%s (heard %d times, last: %s)",
                v.name.c_str(), v.hearCount, v.lastHeard.c_str());
        }

        if (ImGui::Button("Save Profiles##uid")) {
            userIdentifier_->Save("application/resource/memory");
        }
    }
}

std::string MemoryPanel::BuildMemoryContext() const {
    std::string context;

    std::string summary = conversationMemory_->GetSummary();
    if (!summary.empty()) {
        context += "## これまでの会話の要約\n" + summary + "\n\n";
    }

    std::string ltmContext = lastUserMessage_.empty()
        ? longTermMemory_->BuildContextString()
        : longTermMemory_->BuildContextString(lastUserMessage_);
    if (!ltmContext.empty()) {
        context += ltmContext;
    }

    auto& faces = userIdentifier_->FaceProfiles();
    auto& voices = userIdentifier_->VoiceProfiles();
    if (!faces.empty() || !voices.empty()) {
        context += "\n## 認識済みユーザー\n";
        for (auto& f : faces) {
            context += "- [顔] " + f.name + " (目撃 " + std::to_string(f.sightCount) + "回)\n";
        }
        for (auto& v : voices) {
            context += "- [声] " + v.name + " (聞取 " + std::to_string(v.hearCount) + "回)\n";
        }
    }

    return context;
}

void MemoryPanel::NotifyUserMessage(const std::string& message) {
    lastUserMessage_ = message;
    messagesSinceLastExtraction_++;
}

std::string MemoryPanel::BuildFactExtractionPrompt(const std::vector<std::string>& recentMessages) {
    std::ostringstream prompt;
    prompt << "以下の会話から、長期的に覚えておくべき重要な事実を抽出してください。\n"
           << "ユーザーの好み、決定事項、個人情報、習慣、目標、悩みなどを探してください。\n\n"
           << "【会話内容】\n";
    for (auto& msg : recentMessages) {
        prompt << msg << "\n";
    }
    prompt << "\n【ルール】\n"
           << "- 各事実を1行ずつ「カテゴリ|内容」の形式で出力\n"
           << "- カテゴリ: 好み, 決定, 個人情報, 習慣, 目標, 悩み, スキル, 関係, その他\n"
           << "- 重要でない雑談は無視\n"
           << "- 事実がなければ「なし」とだけ回答\n"
           << "- 余計な説明は不要\n\n"
           << "【出力例】\n"
           << "好み|コーヒーよりお茶が好き\n"
           << "目標|来月までにポートフォリオを完成させたい\n"
           << "スキル|C++とPythonが得意\n\n"
           << "出力:";
    return prompt.str();
}

void MemoryPanel::TriggerFactExtraction() {
    if (!localLLM_ || !localLLM_->IsModelLoaded() || localLLM_->IsProcessing()) return;
    if (factExtractionActive_) return;

    auto& entries = conversationMemory_->GetRecentEntries();
    if (entries.empty()) return;

    std::vector<std::string> messages;
    int start = static_cast<int>(entries.size()) - factExtractionThreshold_;
    if (start < 0) start = 0;
    for (int i = start; i < static_cast<int>(entries.size()); ++i) {
        messages.push_back(entries[i].role + ": " + entries[i].content);
    }

    std::string prompt = BuildFactExtractionPrompt(messages);
    factExtractionActive_ = true;
    messagesSinceLastExtraction_ = 0;
    factExtractionFuture_ = localLLM_->GenerateAsync(prompt);
}

void MemoryPanel::PollFactExtraction() {
    if (!factExtractionActive_ || !factExtractionFuture_.valid()) return;
    if (factExtractionFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    std::string result = factExtractionFuture_.get();
    factExtractionActive_ = false;

    if (result.find("\xe3\x81\xaa\xe3\x81\x97") != std::string::npos && result.size() < 20) return;

    std::istringstream ss(result);
    std::string line;
    while (std::getline(ss, line)) {
        auto sep = line.find('|');
        if (sep == std::string::npos) continue;

        std::string category = line.substr(0, sep);
        std::string content = line.substr(sep + 1);

        auto trim = [](std::string& s) {
            auto start = s.find_first_not_of(" \t\r\n");
            auto end = s.find_last_not_of(" \t\r\n");
            if (start == std::string::npos) { s.clear(); return; }
            s = s.substr(start, end - start + 1);
        };
        trim(category);
        trim(content);

        if (!category.empty() && !content.empty() && content.size() < 200) {
            longTermMemory_->AddFact(category, content, 1.5f);
        }
    }
}
