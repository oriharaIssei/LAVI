#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "LLMClient.h"
#include "SpeechSynthesisPipeline.h"

struct SharedMediaContext;

class LLMChatPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    SharedMediaContext* ctx_ = nullptr;

    std::unique_ptr<LLMClient> llmClient_;
    std::string llmUserInput_;
    std::string llmStreamingText_;
    std::future<LLMResponse> llmFuture_;
    bool isLLMProcessing_ = false;
    bool llmAutoScroll_ = true;
    LLMResponse lastLLMResponse_;
    bool llmUseWhisper_ = false;
    bool llmAttachWebCam_ = false;
    bool llmAttachScreen_ = false;
    std::mutex llmStreamMutex_;

    bool llmSpeakResponse_ = false;
    bool llmPlayFiller_ = true;
    SpeechSynthesisPipeline synthPipeline_;

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
};
