#include "TurnSystem.h"

#include "TurnController.h"
#include "MicrophonePanel.h"
#include "GatekeeperManager.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"

TurnSystem::TurnSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {} // 発話状態のターン遷移

TurnSystem::~TurnSystem() = default;

void TurnSystem::Initialize() {
    turnController_ = std::make_unique<TurnController>();
    turnController_->LoadConfig("application/resource/turn/turn_config.json");

    turnController_->SetOnUserSpeechStart([this]() {
        // 発話開始: このターン分の音声を取り直すためバッファをクリア
        if(micPanel_) micPanel_->ClearAudioBuffer();
    });
    turnController_->SetOnUserSpeechEnd([this]() {
        // 発話終了: 自動で転写を開始（結果は MicrophonePanel が ctx.transcribedText に反映）
        if(micPanel_) micPanel_->RequestTranscribe();
    });
    turnController_->SetOnBargeIn([]() {
        // LAVI 発話中にユーザーが割り込んだ → TTS を即停止してユーザーを優先
        SharedMediaContext& ctx = LaviContext::Get();
        if(ctx.voiceVox){ ctx.voiceVox->Stop(); }
        ctx.isSpeaking = false;
        // （この直後に onUserSpeechStart が呼ばれ、音声バッファはクリアされる）
    });

    lastTurnTick_ = std::chrono::steady_clock::now();

    LaviContext::Get().turnController = turnController_.get(); // UI 等は LaviContext 経由で参照
}

void TurnSystem::Finalize() {
    LaviContext::Get().turnController = nullptr;
    turnController_.reset();
}

void TurnSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();
    micPanel_ = ctx.micPanel;     // モノリスが公開する所有インスタンスを参照
    gkManager_ = ctx.gkManager;
    if(!micPanel_ || !gkManager_ || !turnController_) return;

    // TTS 完了処理: 非同期発話が終わったら isSpeaking を下ろす
    if(ctx.isSpeaking && ctx.speakFuture.valid() &&
       ctx.speakFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready){
        ctx.speakFuture.get();
        ctx.isSpeaking = false;
    }

    // 発話区間検出・ターン制御の駆動（メインスレッドで毎フレーム）
    {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTurnTick_).count();
        lastTurnTick_ = now;
        if(dt > 0.5f) dt = 0.5f; // 初回や停止後のスパイクをクランプ

        if(micPanel_->IsCapturing()){
            turnController_->Update(micPanel_->GetCurrentLevel(), dt, ctx.isSpeaking);
        }

        // LAVI 発話状態(isSpeaking)のエッジでターン状態を遷移
        if(ctx.isSpeaking && !prevSpeaking_) turnController_->NotifyResponseStarted();
        if(!ctx.isSpeaking && prevSpeaking_) turnController_->NotifyResponseEnded();
        prevSpeaking_ = ctx.isSpeaking;

        // 応答が来ない場合に Processing で固まらないようにする取りこぼし対策
        if(turnController_->State() == TurnState::Processing){
            if(!processingActive_){ processingActive_ = true; processingSince_ = now; }
            else if(!ctx.isSpeaking && !gkManager_->LlmBusy() &&
                    std::chrono::duration<float>(now - processingSince_).count() > 8.0f){
                // 応答が来ない場合のみ待機へ戻す（LLM 処理中は待つ）
                turnController_->Reset();
                processingActive_ = false;
            }
        } else{
            processingActive_ = false;
        }
    }

    // 発話区間検出で確定 → 転写完了したら、GateKeeper 経由で LLM へ自動送信。
    // 転写は micPanel(Render フェーズ) が更新するため本 System(StateTransition) からは1フレーム遅れて
    // 見えるが、非同期転写は複数フレームに跨るため実用上問題ない。
    if(turnController_->State() == TurnState::Processing &&
       !ctx.transcribedText.empty() &&
       ctx.transcribedText != lastTurnTranscript_ &&
       !gkManager_->LlmBusy()){
        lastTurnTranscript_ = ctx.transcribedText;
        gkManager_->RespondToSpeech();
    }
}
