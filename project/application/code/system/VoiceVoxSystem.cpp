#include "VoiceVoxSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "VoiceVoxClient.h"

#include <chrono>

VoiceVoxSystem::VoiceVoxSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {} // サービス提供（発話自体は他 System が駆動）

VoiceVoxSystem::~VoiceVoxSystem() = default;

void VoiceVoxSystem::Initialize() {
    SharedMediaContext& ctx = LaviContext::Get();
    voiceVox_      = std::make_unique<VoiceVoxClient>();
    ctx.voiceVox   = voiceVox_.get(); // gkManager / TurnSystem / DEBUG UI が参照

    // エンジン起動は最大 30 秒ブロックするためバックグラウンドで仕掛ける（startup を止めない）。
    startFuture_      = std::async(std::launch::async,
                                   [this]() { return voiceVox_->StartEngine(enginePath_); });
    engineStartKicked_ = true;
}

void VoiceVoxSystem::Update() {
    if (!voiceVox_) {
        return;
    }
    // 非同期起動の完了取り込み（結果は IsEngineReady で見るので get で残骸を回収するだけ）。
    if (startFuture_.valid() &&
        startFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        (void)startFuture_.get();
    }
    // エンジンが ready になったら一度だけ話者一覧を取得して公開する。
    if (!speakersFetched_ && voiceVox_->IsEngineReady()) {
        LaviContext::Get().voiceVoxSpeakers = voiceVox_->GetSpeakers();
        speakersFetched_ = true;
    }
}

void VoiceVoxSystem::Finalize() {
    SharedMediaContext& ctx = LaviContext::Get();
    if (voiceVox_) {
        voiceVox_->Stop(); // 再生中の発話を停止
    }
    if (startFuture_.valid()) {
        startFuture_.wait(); // 非同期起動の完了を待つ（解放前に）
    }
    if (ctx.speakFuture.valid()) {
        ctx.speakFuture.wait();
        ctx.isSpeaking = false;
    }
    if (voiceVox_) {
        voiceVox_->StopEngine();
    }
    ctx.voiceVox = nullptr;
    voiceVox_.reset();
}
