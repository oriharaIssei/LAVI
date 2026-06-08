#include "MicGateSystem.h"

#include "MicGatekeeper.h"
#include "GatekeeperManager.h" // GateSource, Config
#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturePromptComponent.h"

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

MicGateSystem::MicGateSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {}

MicGateSystem::~MicGateSystem() = default;

void MicGateSystem::Initialize() {
    mic_ = std::make_unique<MicGatekeeper>();
    LaviContext::Get().micGate = mic_.get();
    lastEval_ = std::chrono::steady_clock::now();

    // JSON 設定からキーワードを適用（Release でも反映。従来は DEBUG パネルでしか設定されなかった）。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    mic_->SetKeywords(SplitGatekeeperKeywords(cfg.keywordsText));
    mic_->SetCaseSensitive(cfg.caseSensitive);
}

void MicGateSystem::Finalize() {
    LaviContext::Get().micGate = nullptr;
    mic_.reset();
}

void MicGateSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();
    GatekeeperManager* gk = ctx.gkManager;
    if(!gk || !mic_) return;

    const auto& cfg = gk->config();
    if(!cfg.micEnabled) return;

    const auto now = std::chrono::steady_clock::now();
    if(std::chrono::duration<float>(now - lastEval_).count() < cfg.micInterval) return;
    lastEval_ = now;

    ctx.micResult = mic_->Evaluate(ctx.transcribedText);
    if(!ctx.micResult.triggered) return;

    auto e = CreateEntity("CapturePrompt");
    AddComponent<CapturePromptComponent>(e);
    if(auto* c = GetComponent<CapturePromptComponent>(e)){
        c->source = static_cast<int>(GateSource::Mic);
        c->description = "キーワード検知: " + Join(ctx.micResult.matched, ", ");
        c->time = std::chrono::duration<double>(now.time_since_epoch()).count();
    }
}
