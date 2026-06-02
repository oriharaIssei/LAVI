#pragma once

#include "system/ISystem.h"

#include "TurnController.h"

#include <chrono>
#include <memory>

struct SharedMediaContext;
class MicrophonePanel;
class WebCameraPanel;
class ScreenCapturePanel;
class VoiceVoxPanel;
class VisionPanel;
class LLMChatPanel;
class GatekeeperManager;
class GatekeeperPanel;
class MemoryPanel;
class SentenceEmbedding;
class KnowledgeBase;
class WebSearchClient;
class ActionPipeline;
class LocationProvider;

class MediaCaptureDemoSystem
    : public OriGine::ISystem
{
public:
    MediaCaptureDemoSystem();
    ~MediaCaptureDemoSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<SharedMediaContext> ctx_;
    std::unique_ptr<MicrophonePanel> micPanel_;
    std::unique_ptr<WebCameraPanel> camPanel_;
    std::unique_ptr<ScreenCapturePanel> screenPanel_;
    std::unique_ptr<VoiceVoxPanel> voiceVoxPanel_;
    std::unique_ptr<VisionPanel> visionPanel_;
    std::unique_ptr<LLMChatPanel> llmPanel_;
    std::unique_ptr<GatekeeperManager> gkManager_;
    std::unique_ptr<GatekeeperPanel> gkPanel_;
    std::unique_ptr<MemoryPanel> memoryPanel_;
    std::unique_ptr<SentenceEmbedding> embedding_;
    std::unique_ptr<KnowledgeBase> knowledgeBase_;
    std::unique_ptr<WebSearchClient> webSearch_;
    std::unique_ptr<ActionPipeline> actionPipeline_;
    std::unique_ptr<LocationProvider> location_;

    bool hotkeyRegistered_ = false;
    std::string lastWakeWordText_;

    // 発話区間検出・ターン制御
    std::unique_ptr<TurnController> turnController_;
    std::chrono::steady_clock::time_point lastTurnTick_{};
    std::chrono::steady_clock::time_point processingSince_{};
    bool prevSpeaking_ = false;
    bool processingActive_ = false;
    std::string lastTurnTranscript_; // GateKeeper へ自動送信済みの発話（重複送信防止）
    void DrawTurnControlUI();

    // 知識ベース RAG の管理 UI 用の入力バッファ
    std::string kbSourceBuf_;
    std::string kbTextBuf_;
    std::string kbStatus_;
    void DrawKnowledgeBaseUI();
};
