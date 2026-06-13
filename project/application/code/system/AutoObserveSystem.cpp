#include "AutoObserveSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "GatekeeperManager.h"

#include "LocalVisionAnalyzer.h"
#include "FrameHistory.h"
#include "mediaCapture/WebCamera.h"
#include "mediaCapture/ScreenCapture.h"

#include <memory>
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

    // 「特になし」だけの応答は喋らせない（観察プロンプトの取り決め）。
    auto maybeSpeak = [&](const std::string& content) {
        if (!content.empty() && content.find("特になし") == std::string::npos) {
            gk->SpeakProactive(content);
        }
    };

    // 進行中リクエストの回収（enabled に関わらず in-flight は必ず回収する）。
    if (busy_) {
        if (busyLocal_) {
            if (localFuture_.valid() &&
                localFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                VisionResult res = localFuture_.get();
                busy_ = false; busyLocal_ = false;
                if (res.success) maybeSpeak(res.description);
            }
        } else {
            if (future_.valid() &&
                future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                LLMResponse res = future_.get();
                busy_ = false;
                if (res.success) maybeSpeak(res.content);
            }
        }
        return;
    }

    if (!cfg.autoObserveEnabled) {
        return;
    }

    // 経路選択：ローカル VLM がロード済みなら local、そうでなければ cloud。
    const bool useLocal = cfg.visionUseLocal && ctx.localVisionAnalyzer &&
                          ctx.localVisionAnalyzer->IsModelLoaded();

    // GK 応答中 / LAVI 発話中は見送る。クラウド経路は API キー必須（ローカルは不要）。
    if (gk->LlmBusy() || ctx.isSpeaking) {
        return;
    }
    if (!useLocal && ctx.config.apiKey.empty()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - lastTime_).count() < cfg.autoObserveInterval) {
        return;
    }
    lastTime_ = now;

    // 観察対象フレームを収集（撮影中のソースのみ）。動画モードでは各ソースの直近フレームを
    // 時系列に複数枚（FrameHistory::Sample(K)）送って動き・変化を解釈させる。
    // frames が .data() を参照する間、サンプルした不変スナップショットを holds で生存させる。
    const bool videoMode = ctx.config.visionVideoEnabled;
    const int sampleK    = (ctx.config.visionVideoFrames < 1) ? 1 : ctx.config.visionVideoFrames;

    std::vector<LLMClient::ImageFrame> frames;           // cloud 用
    std::vector<LocalVisionAnalyzer::Frame> localFrames; // local 用
    std::vector<std::shared_ptr<const CapturedFrame>> holds;

    auto addSource = [&](FrameHistory* hist, const std::shared_ptr<const CapturedFrame>& latest) {
        std::vector<std::shared_ptr<const CapturedFrame>> samples;
        if (videoMode && hist) {
            samples = hist->Sample(sampleK);
        } else if (latest) {
            samples.push_back(latest);
        }
        for (auto& f : samples) {
            if (!f || f->pixels.empty()) continue;
            holds.push_back(f);
            frames.push_back({f->pixels.data(), f->width, f->height});
            localFrames.push_back({f->pixels.data(), f->width, f->height});
        }
    };

    if (cfg.autoObserveWebCam) addSource(ctx.cameraHistory, ctx.cameraFrame);
    if (cfg.autoObserveScreen) addSource(ctx.screenHistory, ctx.screenFrame);

    if (frames.empty()) {
        return; // 観察できるソースが無い
    }

    const std::string observePrompt =
        videoMode
            ? std::string("[自律観察] 以下は直近数秒の時系列フレーム（古い順）です。カメラ→画面の順で並んでいます。"
                          "動きや変化・進行に注目して、気になることやコメントしたいことがあれば自然に話しかけてください。"
                          "特に何もなければ「特になし」とだけ答えてください。")
            : std::string("[自律観察] 今のカメラ/画面の様子を見てください。"
                          "何か気になること、面白いこと、コメントしたいことがあれば自然に話しかけてください。"
                          "特に何もなければ「特になし」とだけ答えてください。");

    if (useLocal) {
        LocalVisionAnalyzer* lv = ctx.localVisionAnalyzer;
        lv->SetSystemPrompt(ctx.config.llmSystemPrompt); // 現在の人格で観察させる
        lv->SetPrompt(observePrompt);
        localFuture_ = lv->AnalyzeAsync(localFrames); // カメラ/主モニタの時系列フレームを複数枚で
        busy_ = true; busyLocal_ = true;
    } else {
        llm_.SetApiKey(ctx.config.apiKey);
        llm_.SetSystemPrompt(ctx.config.llmSystemPrompt);
        llm_.ClearHistory();
        llm_.AddMessageWithImages("user", observePrompt, frames);
        future_ = llm_.SendAsync();
        busy_ = true; busyLocal_ = false;
    }
}
