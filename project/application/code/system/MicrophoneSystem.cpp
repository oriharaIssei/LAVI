#include "MicrophoneSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "MicrophonePanel.h"
#include "MicrophoneConfig.h"

#include <chrono>

MicrophoneSystem::MicrophoneSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {} // 発話のキャプチャ（ユーザー入力）

MicrophoneSystem::~MicrophoneSystem() = default;

void MicrophoneSystem::Initialize() {
    SharedMediaContext& ctx = LaviContext::Get();
    micPanel_ = std::make_unique<MicrophonePanel>();
    micPanel_->Initialize(&ctx);
    ctx.micPanel = micPanel_.get(); // TurnSystem 等が参照（非所有ポインタ公開）

    // 設定に従い、起動時に自動でマイク撮影開始＋Whisper モデルロード（Release で音声会話を可能に）。
    const MicrophoneConfigData cfg = LoadMicrophoneConfig();
    if (cfg.autoStartCapture) {
        micPanel_->StartDefaultCapture(cfg.micDeviceIndex); // 撮影開始（レベル計測=TurnController に必要）
    }
    if (cfg.autoLoadModel) {
        // Whisper モデルは大きく LoadModel がブロックするため、起動を止めないようバックグラウンドでロードする。
        // ロード完了まで音声コールバックは IsModelLoaded()==false で PushAudio をスキップするので安全。
        modelLoadFuture_ = std::async(std::launch::async,
            [this, m = cfg.whisperModelPath, v = cfg.vadModelPath]() {
                return micPanel_->LoadWhisperModel(m, v);
            });
    }
}

void MicrophoneSystem::Update() {
    // 非同期モデルロードの完了を回収（残骸処理のみ。状態は IsModelLoaded で参照）。
    if (modelLoadFuture_.valid() &&
        modelLoadFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        (void)modelLoadFuture_.get();
    }
    // 非同期処理(転写/校正/集約)の完了取り込み。タブ非表示でも毎フレーム必須（自動転写のため）。
    micPanel_->Update();
}

void MicrophoneSystem::Finalize() {
    if (modelLoadFuture_.valid()) {
        modelLoadFuture_.wait(); // ロード中なら完了を待ってから破棄（解放前の参照を防ぐ）
    }
    LaviContext::Get().micPanel = nullptr; // 解放前に公開ポインタを無効化（参照先消滅を防ぐ）
    micPanel_->Finalize();
    micPanel_.reset();
}
