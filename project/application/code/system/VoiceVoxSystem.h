#pragma once

#include "system/ISystem.h"

#include <future>
#include <memory>
#include <string>

class VoiceVoxClient;

/// <summary>
/// 音声合成(VoiceVox)エンジンのライフサイクルを管理する ECS システム（Category=Initialize）。
/// VoiceVoxClient を所有して LaviContext::Get().voiceVox に公開し、エンジンを**非同期**で起動する
/// （StartEngine は最大 30 秒ブロックするため、startup を止めないよう std::async でバックグラウンド起動）。
/// 起動完了後に話者一覧を ctx.voiceVoxSpeakers へ取得する。
/// 旧 VoiceVoxPanel が DEBUG 限定だったため Release で TTS が起動しなかった問題の解消（ECS 分解）。
/// 実際の発話(SpeakAsync)は gkManager / TurnSystem が ctx.voiceVox 経由で行い、発話完了処理は TurnSystem。
/// 調整 UI（VoiceVoxPanel::Draw）は DEBUG 限定で ctx.voiceVox を pull して操作する。
/// </summary>
class VoiceVoxSystem : public OriGine::ISystem {
public:
    VoiceVoxSystem();
    ~VoiceVoxSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<VoiceVoxClient> voiceVox_; // TTS クライアントの所有（旧パネルから移管）
    std::future<bool> startFuture_;            // 非同期エンジン起動
    bool engineStartKicked_ = false;           // 起動を一度だけ仕掛けたか
    bool speakersFetched_   = false;           // 話者一覧取得済みか

    // 既定エンジンパス（DEBUG パネルと同一既定。将来 config 化候補）。
    std::string enginePath_ = "application/resource/voiceVox/windows-nvidia/run.exe";
};
