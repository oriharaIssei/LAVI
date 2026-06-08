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
class KnowledgeBase;
class WebSearchClient;
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
    SharedMediaContext* ctx_ = nullptr; // LaviContext シングルトンを指す（非所有）
    // MicrophonePanel は MicrophoneSystem(Input)、MemoryPanel は MemorySystem(Movement) が所有（ECS 分解）。
    // Draw は LaviContext::Get().micPanel / .memoryPanel を pull する。
    std::unique_ptr<WebCameraPanel> camPanel_;
    std::unique_ptr<ScreenCapturePanel> screenPanel_;
    std::unique_ptr<VoiceVoxPanel> voiceVoxPanel_;
    std::unique_ptr<VisionPanel> visionPanel_;
    std::unique_ptr<LLMChatPanel> llmPanel_;
    // GatekeeperManager は GatekeeperSystem へ移譲（ECS 分解）。UI は LaviContext::Get().gkManager 経由。
    std::unique_ptr<GatekeeperPanel> gkPanel_;
    // MemoryPanel は MemorySystem へ移譲（上記コメント参照）。
    // SentenceEmbedding / KnowledgeBase は KnowledgeSystem へ移譲（ECS 分解）。共有は LaviContext::Get().knowledgeBase / .embedding 経由。
    // WebSearchClient は WebSearchSystem へ移譲（ECS 分解）。共有は LaviContext::Get().webSearch 経由。
    // ActionPipeline は ActionSystem へ移譲（ECS 分解）。共有は LaviContext::Get().actionPipeline 経由。
    // LocationProvider は LocationSystem へ移譲（ECS 分解）。共有は LaviContext::Get().location 経由。

    // config/タグ軸ロード・ホットキー登録・システムトレイは AppLifecycleSystem へ移譲（ECS 分解）。
    // ウェイクワード検出は WakeWordSystem へ移譲（ECS 分解）。

    // 発話区間検出・ターン制御は TurnSystem へ移譲（ECS 分解）。共有は LaviContext::Get().turnController 経由。
    // 編集UI(DrawTurnControlUI)のみ TurnController を参照するため前方宣言は残す。
    void DrawTurnControlUI();

    // 知識ベース RAG の管理 UI 用の入力バッファ
    std::string kbSourceBuf_;
    std::string kbTextBuf_;
    std::string kbStatus_;
    void DrawKnowledgeBaseUI();
};
