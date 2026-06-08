#pragma once

#include <string>

/// <summary>
/// マイク入力・音声認識(Whisper)の起動設定。GlobalVariables(Scene="Settings", Group="Microphone")
/// に永続化し、MicrophoneSystem が起動時に読み込んで自動でマイク開始・モデルロードを行う。
/// 従来 DEBUG パネルのボタン依存だったため Release で音声会話が始まらなかった問題を解消する
/// （mem:feedback_no_hardcode / mem:project_ecs_decomposition）。
/// </summary>
struct MicrophoneConfigData {
    bool autoStartCapture = true;  // 起動時に既定デバイスを開いて撮影開始するか
    bool autoLoadModel    = true;  // 起動時に Whisper モデルを自動ロードするか
    int micDeviceIndex    = -1;    // 使用デバイス番号（-1=既定デバイス）
    std::string whisperModelPath = "application/resource/whisper/ggml-large-v3.bin";
    std::string vadModelPath     = "application/resource/whisper/ggml-silero-v6.2.0.bin";
};

MicrophoneConfigData LoadMicrophoneConfig();
void SaveMicrophoneConfig(const MicrophoneConfigData& config);
