#include "ScreenGateSystem.h"

#include "ScreenGatekeeper.h"
#include "GatekeeperManager.h" // GateSource, Config
#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturePromptComponent.h"

#include "mediaCapture/ScreenCapture.h" // OriGine::ScreenCapture 完全定義

#include <cstdio>

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
    LaviContext::Get().screenGate = nullptr;
    screen_.reset();
}

void ScreenGateSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();
    GatekeeperManager* gk = ctx.gkManager;
    if(!gk || !screen_) return;

    const auto& cfg = gk->config();
    if(!cfg.screenEnabled) return;

    const auto now = std::chrono::steady_clock::now();
    if(std::chrono::duration<float>(now - lastEval_).count() < cfg.screenInterval) return;
    lastEval_ = now;

    const uint8_t* data = nullptr;
    uint32_t fw = 0, fh = 0;
    if(ctx.screenCapture && ctx.screenCapture->IsCapturing()){
        if(ctx.screenCapture->GetLatestFrame(ctx.screenFrameBuffer, fw, fh) && fw > 0 && fh > 0){
            data = ctx.screenFrameBuffer.data();
        } else{
            fw = fh = 0;
        }
    }

    ctx.screenResult = screen_->Evaluate(data, fw, fh);
    const ScreenGateResult& r = ctx.screenResult;
    if(!r.triggered) return;

    std::string d;
    if(r.foregroundChanged) d += "アプリ切替: " + r.foregroundApp;
    if(!r.newApps.empty()){
        if(!d.empty()) d += " / ";
        d += "新規起動: " + Join(r.newApps, ",");
    }
    if(r.screenChanged){
        if(!d.empty()) d += " / ";
        char b[32];
        std::snprintf(b, sizeof(b), "画面変化 %.1f%%", r.screenDiffRatio * 100.0f);
        d += b;
    }
    if(d.empty()) d = "画面イベント";

    auto e = CreateEntity("CapturePrompt");
    AddComponent<CapturePromptComponent>(e);
    if(auto* c = GetComponent<CapturePromptComponent>(e)){
        c->source = static_cast<int>(GateSource::Screen);
        c->description = std::move(d);
        c->time = std::chrono::duration<double>(now.time_since_epoch()).count();
    }
}
