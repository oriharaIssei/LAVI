#include "AutoObserveSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "GatekeeperManager.h"

#include "LocalVisionAnalyzer.h"
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

    // 観察対象フレームを収集（撮影中のソースのみ）。cloud は複数枚、local は先頭1枚を使う。
    // カメラ＋撮影中の全モニタを集める。cloud/local とも複数画像を渡す。
    // GetLatestFrame の出力先は画像ごとに別バッファが要るため、ここで保持する
    // （frames が .data() を参照するので reserve で再確保＝ポインタ無効化を防ぐ）。
    std::vector<LLMClient::ImageFrame> frames;          // cloud 用
    std::vector<LocalVisionAnalyzer::Frame> localFrames; // local 用
    std::vector<std::vector<uint8_t>> screenBufs;
    screenBufs.reserve(ctx.screenCaptures.size());
    uint32_t fw = 0, fh = 0;

    if (cfg.autoObserveWebCam && ctx.webCamera && ctx.webCamera->IsCapturing()) {
        if (ctx.webCamera->GetLatestFrame(ctx.camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
            frames.push_back({ctx.camFrameBuffer.data(), fw, fh});
            localFrames.push_back({ctx.camFrameBuffer.data(), fw, fh});
        }
    }
    if (cfg.autoObserveScreen) {
        for (OriGine::ScreenCapture* sc : ctx.screenCaptures) {
            if (!sc || !sc->IsCapturing()) continue;
            screenBufs.emplace_back();
            if (sc->GetLatestFrame(screenBufs.back(), fw, fh) && fw > 0 && fh > 0) {
                frames.push_back({screenBufs.back().data(), fw, fh});
                localFrames.push_back({screenBufs.back().data(), fw, fh});
            } else {
                screenBufs.pop_back();
            }
        }
    }
    if (frames.empty()) {
        return; // 観察できるソースが無い
    }

    const std::string observePrompt =
        "[自律観察] 今のカメラ/画面の様子を見てください。"
        "何か気になること、面白いこと、コメントしたいことがあれば自然に話しかけてください。"
        "特に何もなければ「特になし」とだけ答えてください。";

    if (useLocal) {
        LocalVisionAnalyzer* lv = ctx.localVisionAnalyzer;
        lv->SetSystemPrompt(ctx.config.llmSystemPrompt); // 現在の人格で観察させる
        lv->SetPrompt(observePrompt);
        localFuture_ = lv->AnalyzeAsync(localFrames); // カメラ＋全モニタを複数画像で
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
