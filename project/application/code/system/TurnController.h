#pragma once

#include <cstdint>
#include <functional>
#include <string>

/// <summary>
/// ターン制御の状態。
/// </summary>
enum class TurnState : uint8_t {
    Idle,         // 待機（誰も話していない）
    UserSpeaking, // ユーザー発話中
    Processing,   // 発話終了 → 転写/LLM 処理中（応答待ち）
    LaviSpeaking, // LAVI 応答（TTS）再生中
};

/// <summary>
/// 発話区間検出・ターン制御のチューニング値。JSON から読み込む（ハードコードしない）。
/// </summary>
struct TurnConfig {
    bool enabled         = true;    // ターン制御を有効にするか
    bool bargeInEnabled  = true;    // LAVI 発話中の割り込みを許可するか

    float startThreshold = 0.020f;  // 発話開始とみなす RMS
    float endThreshold   = 0.012f;  // これ未満を無音とみなす RMS（ヒステリシス: start > end）
    float minSpeechMs    = 200.0f;  // これ未満の有音は無視（ノイズ・破裂音の除去）
    float silenceHangoverMs = 800.0f; // 発話終了とみなす無音の継続時間（「間」の長さ）
    float bargeInMs      = 300.0f;  // LAVI 発話中、これだけ有音が続いたら割り込みと判定
};

/// <summary>
/// 音声エネルギー（RMS）の時系列から発話区間を検出し、会話のターン状態を管理する。
/// 純ロジック（DirectX/音声デバイス非依存）。所有者が毎フレーム RMS を投入し、
/// コールバックで発話開始/終了/割り込みを受け取って、転写・LLM・TTS を駆動する。
/// </summary>
class TurnController {
public:
    using VoidCallback = std::function<void()>;

    bool LoadConfig(const std::string& path); // JSON 読み込み（無ければ既定値）
    void SaveConfig() const;
    TurnConfig& Config() { return config_; }
    const TurnConfig& Config() const { return config_; }

    /// <summary>
    /// 毎フレーム呼ぶ。rms=現在の音声 RMS, dtSeconds=前フレームからの経過秒,
    /// laviSpeaking=TTS が現在再生中か。
    /// </summary>
    void Update(float rms, float dtSeconds, bool laviSpeaking);

    // 状態遷移の外部通知（所有者が転写/LLM/TTS の進行に合わせて呼ぶ）
    void NotifyResponseStarted(); // Processing → LaviSpeaking（TTS 開始時）
    void NotifyResponseEnded();   // LaviSpeaking → Idle（TTS 終了時）
    void Reset();                 // 強制的に Idle へ（中断・無応答時など）

    // イベントコールバック
    void SetOnUserSpeechStart(VoidCallback cb) { onStart_ = std::move(cb); }
    void SetOnUserSpeechEnd(VoidCallback cb) { onEnd_ = std::move(cb); }
    void SetOnBargeIn(VoidCallback cb) { onBargeIn_ = std::move(cb); }

    TurnState State() const { return state_; }
    float CurrentRms() const { return lastRms_; }
    bool IsUserSpeaking() const { return state_ == TurnState::UserSpeaking; }

private:
    TurnConfig config_;
    TurnState state_ = TurnState::Idle;

    float speechRunMs_  = 0.0f; // 連続有音時間
    float silenceRunMs_ = 0.0f; // 連続無音時間
    float lastRms_      = 0.0f;

    std::string path_;
    VoidCallback onStart_;
    VoidCallback onEnd_;
    VoidCallback onBargeIn_;
};
