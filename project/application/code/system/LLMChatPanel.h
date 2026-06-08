#pragma once

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "LLMClient.h"
#include "SpeechSynthesisPipeline.h"

struct SharedMediaContext;
class MemoryPanel;
class KnowledgeBase;
class ActionPipeline;
class WebSearchClient;
class LocationProvider;

class LLMChatPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void SetMemoryPanel(MemoryPanel* memPanel);
    // 埋め込み/知識ベースは KnowledgeSystem、アクションは ActionSystem が LaviContext に公開。
    // 各処理の入口で遅延参照する（Set 廃止）。
    // 時事Web検索・現在地は WebSearchSystem / LocationSystem が LaviContext に公開。
    // SendLLMRequest 内で LaviContext から遅延参照する（SetWebSearch / SetLocation 廃止）。
    void Finalize();
    void Draw();

    void SavePanelState();
    void LoadPanelState();

private:
    SharedMediaContext* ctx_ = nullptr;

    std::unique_ptr<LLMClient> llmClient_;
    std::string llmUserInput_;
    std::string llmStreamingText_;
    std::future<LLMResponse> llmFuture_;
    bool isLLMProcessing_ = false;

    // バックエンド切替: クラウド Claude / ローカル LLM（MemoryPanel の共有インスタンスを再利用）
    bool useLocalLLM_ = false;
    bool localEnableThinking_ = false; // reasoning モデルの思考を使うか（既定 OFF＝高速）
    std::future<std::string> localFuture_;
    LLMStreamCallback activeStreamCb_;
    bool llmAutoScroll_ = true;
    LLMResponse lastLLMResponse_;
    bool llmUseWhisper_ = false;
    bool llmAttachWebCam_ = false;
    bool llmAttachScreen_ = false;
    std::mutex llmStreamMutex_;

    bool llmSpeakResponse_ = false;
    bool llmPlayFiller_ = true;
    SpeechSynthesisPipeline synthPipeline_;

    bool llmAttachAppInfo_ = false;
    bool llmEnableWebSearch_ = false;
    // この応答が「検索して答える」モードか。true の間は [browser:]/[action:] を実行せず除去する
    // （情報回答中に動画再生などのアクションが暴発するのを防ぐ）。
    std::atomic<bool> suppressActionsForResponse_{false};

    std::string speechTagBuf_;
    bool inActionTag_ = false;

    bool llmAutoObserve_ = false;
    bool llmAutoObserveWebCam_ = true;
    bool llmAutoObserveScreen_ = false;
    float llmAutoObserveInterval_ = 10.0f;
    std::chrono::steady_clock::time_point llmAutoObserveLastTime_;

    void SendLLMRequest(const std::string& text, const std::vector<LLMClient::ImageFrame>& frames, bool playFiller = false);
    LLMStreamCallback MakeStreamCallback();

    std::string personaInput_;
    std::unique_ptr<LLMClient> personaClient_;
    std::future<LLMResponse> personaFuture_;
    bool isGeneratingPersona_ = false;
    void GeneratePersonaPrompt();

    MemoryPanel* memoryPanel_ = nullptr;
    bool useMemoryContext_ = true;
    bool useKnowledgeBase_ = true; // 知識ベースRAGを会話に注入するか
    KnowledgeBase* knowledgeBase_ = nullptr; // 共有。KnowledgeSystem 公開。入口で LaviContext から引く
    ActionPipeline* actionPipeline_ = nullptr; // 共有。ActionSystem 公開。入口で LaviContext から引く
    WebSearchClient* webSearch_ = nullptr;
};
