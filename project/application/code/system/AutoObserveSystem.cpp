#include "AutoObserveSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "GatekeeperManager.h"

#include "mediaCapture/WebCamera.h"
#include "mediaCapture/ScreenCapture.h"

#include <string>
#include <vector>

AutoObserveSystem::AutoObserveSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

AutoObserveSystem::~AutoObserveSystem() = default;

void AutoObserveSystem::Initialize() {
    lastTime_ = std::chrono::steady_clock::now();
}

void AutoObserveSystem::Finalize() {
    llm_.Cancel();
    if (future_.valid()) {
        future_.wait();
    }
}

void AutoObserveSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();
    GatekeeperManager* gk = ctx.gkManager;
    if (!gk) {
        return;
    }
    const GatekeeperManager::Config& cfg = gk->config();

    // 進行中リクエストの回収（enabled に関わらず in-flight は必ず回収する）。
    if (busy_) {
        if (future_.valid() &&
            future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            LLMResponse res = future_.get();
            busy_ = false;
            // 「特になし」だけの応答は喋らせない（観察プロンプトの取り決め）。
            if (res.success && !res.content.empty() &&
                res.content.find("特になし") == std::string::npos) {
                gk->SpeakProactive(res.content);
            }
        }
        return;
    }

    if (!cfg.autoObserveEnabled) {
        return;
    }
    // GK 応答中 / LAVI 発話中 / API キー未設定なら見送る（会話と衝突させない）。
    if (gk->LlmBusy() || ctx.isSpeaking || ctx.config.apiKey.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - lastTime_).count() < cfg.autoObserveInterval) {
        return;
    }
    lastTime_ = now;

    // 観察対象フレームを収集（撮影中のソースのみ）。
    std::vector<LLMClient::ImageFrame> frames;
    uint32_t fw = 0, fh = 0;
    if (cfg.autoObserveWebCam && ctx.webCamera && ctx.webCamera->IsCapturing()) {
        if (ctx.webCamera->GetLatestFrame(ctx.camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
            frames.push_back({ctx.camFrameBuffer.data(), fw, fh});
        }
    }
    if (cfg.autoObserveScreen && ctx.screenCapture && ctx.screenCapture->IsCapturing()) {
        if (ctx.screenCapture->GetLatestFrame(ctx.screenFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
            frames.push_back({ctx.screenFrameBuffer.data(), fw, fh});
        }
    }
    if (frames.empty()) {
        return; // 観察できるソースが無い
    }

    const std::string observePrompt =
        "[自律観察] 今のカメラ/画面の様子を見てください。"
        "何か気になること、面白いこと、コメントしたいことがあれば自然に話しかけてください。"
        "特に何もなければ「特になし」とだけ答えてください。";

    llm_.SetApiKey(ctx.config.apiKey);
    llm_.SetSystemPrompt(ctx.config.llmSystemPrompt); // 現在の人格で観察コメントさせる
    llm_.ClearHistory();
    llm_.AddMessageWithImages("user", observePrompt, frames);

    future_ = llm_.SendAsync();
    busy_   = true;
}
