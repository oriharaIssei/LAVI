#include "ScreenGateSystem.h"

#include "ScreenGatekeeper.h"
#include "GatekeeperManager.h" // GateSource, Config
#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturePromptComponent.h"

#include "mediaCapture/ScreenCapture.h" // OriGine::ScreenCapture 完全定義

#include <cstdio>
#include <future>
#include <memory>

namespace {
std::string Join(const std::vector<std::string>& v, const char* sep) {
    std::string r;
    for(size_t i = 0; i < v.size(); ++i){
        if(i) r += sep;
        r += v[i];
    }
    return r;
}
} // namespace

ScreenGateSystem::ScreenGateSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {}

ScreenGateSystem::~ScreenGateSystem() = default;

void ScreenGateSystem::Initialize() {
    screen_ = std::make_unique<ScreenGatekeeper>();
    LaviContext::Get().screenGate = screen_.get();
    lastEval_ = std::chrono::steady_clock::now();

    // JSON 設定から画面 GK パラメータを適用（Release でも反映）。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    screen_->SetWatchForeground(cfg.watchFg);
    screen_->SetDetectNewProcesses(cfg.detectNew);
    screen_->SetScreenDiffThreshold(cfg.screenDiffThreshold);
    screen_->SetPixelDiffThreshold(cfg.pixelDiffThreshold);
}

void ScreenGateSystem::Finalize() {
    if (evalFuture_.valid()) {
        evalFuture_.wait(); // 進行中のワーカーが screen_ を使い終わるのを待つ
    }
    evalBusy_ = false;
    LaviContext::Get().screenGate = nullptr;
    screen_.reset();
}

void ScreenGateSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();
    GatekeeperManager* gk   = ctx.gkManager;
    if (!gk || !screen_) return;

    // 1) 完了した非同期評価を回収し、トリガー時に CapturePrompt を発行する。
    if (evalBusy_ && evalFuture_.valid() &&
        evalFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        ScreenGateResult r = evalFuture_.get();
        evalBusy_          = false;
        ctx.screenResult   = r;
        if (r.triggered) {
            std::string d;
            if (r.foregroundChanged) d += "アプリ切替: " + r.foregroundApp;
            if (!r.newApps.empty()) {
                if (!d.empty()) d += " / ";
                d += "新規起動: " + Join(r.newApps, ",");
            }
            if (r.screenChanged) {
                if (!d.empty()) d += " / ";
                char b[32];
                std::snprintf(b, sizeof(b), "画面変化 %.1f%%", r.screenDiffRatio * 100.0f);
                d += b;
            }
            if (d.empty()) d = "画面イベント";

            auto e = CreateEntity("CapturePrompt");
            AddComponent<CapturePromptComponent>(e);
            if (auto* c = GetComponent<CapturePromptComponent>(e)) {
                c->source      = static_cast<int>(GateSource::Screen);
                c->description = std::move(d);
                c->time        = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            }
        }
    }

    // 2) 間隔ごとに新しい評価を起動（フォアグラウンド/新規起動検知はフレーム不要なので間隔駆動）。
    const auto& cfg = gk->config();
    if (!cfg.screenEnabled) return;
    if (evalBusy_) return;

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - lastEval_).count() < cfg.screenInterval) return;
    lastEval_ = now;

    auto frame             = ctx.screenFrame; // 画面差分用スナップショット（無い場合は null＝差分はスキップされる）
    ScreenGatekeeper* sc   = screen_.get();
    evalBusy_              = true;
    evalFuture_ = std::async(std::launch::async, [sc, frame]() -> ScreenGateResult {
        if (frame && !frame->pixels.empty()) {
            return sc->Evaluate(frame->pixels.data(), frame->width, frame->height);
        }
        return sc->Evaluate(nullptr, 0, 0);
    });
}
