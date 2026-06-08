#include "GatekeeperSystem.h"

#include "GatekeeperManager.h"
#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturePromptComponent.h"

#include "scene/Scene.h"
#include "component/ComponentArray.h"

GatekeeperSystem::GatekeeperSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Movement) {} // 意図分類・ルーティング（行動の決定）

GatekeeperSystem::~GatekeeperSystem() = default;

void GatekeeperSystem::Initialize() {
    gkManager_ = std::make_unique<GatekeeperManager>();
    gkManager_->Initialize(&LaviContext::Get());

    // 統合フラグ・評価間隔・エスカレーション設定を JSON 設定から適用（Release でも反映）。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    auto& mc = gkManager_->config();
    mc.camEnabled      = cfg.camEnabled;
    mc.screenEnabled   = cfg.screenEnabled;
    mc.micEnabled      = cfg.micEnabled;
    mc.camInterval     = cfg.camInterval;
    mc.screenInterval  = cfg.screenInterval;
    mc.micInterval     = cfg.micInterval;
    mc.combineWindow   = cfg.combineWindow;
    mc.autoEscalate    = cfg.autoEscalate;
    mc.escalateCooldown = cfg.escalateCooldown;
    mc.autoSpeak       = cfg.autoSpeak;
    mc.useLocalLLM     = cfg.useLocalLLM;
    mc.useWebContext   = cfg.useWebContext;
    mc.autoObserveEnabled  = cfg.autoObserveEnabled;
    mc.autoObserveWebCam   = cfg.autoObserveWebCam;
    mc.autoObserveScreen   = cfg.autoObserveScreen;
    mc.autoObserveInterval = cfg.autoObserveInterval;

    LaviContext::Get().gkManager = gkManager_.get(); // 消費側は LaviContext 経由で参照
}

void GatekeeperSystem::Finalize() {
    LaviContext::Get().gkManager = nullptr;
    gkManager_.reset();
}

void GatekeeperSystem::Update() {
    if(!gkManager_) return;

    // キャプチャ評価 System が発行した CapturePromptComponent を取り込み（→pending_）、
    // 取り込んだエンティティは破棄する（「会話を発生させるか破棄するか」の入力収集）。
    if(auto* arr = GetComponentArray<CapturePromptComponent>()){
        for(auto& slot : arr->GetSlotsRef()){
            for(auto& c : slot.components){
                gkManager_->IngestPrompt(static_cast<GateSource>(c.source), c.description);
            }
            if(!slot.components.empty() && GetScene()){
                GetScene()->AddDeleteEntity(slot.owner); // 取り込み済みプロンプトは遅延削除
            }
        }
    }

    // 合成・意図分類・LLM 送出・発話（既存ロジックを再利用）。
    gkManager_->Update();
}
