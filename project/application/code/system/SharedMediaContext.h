#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include "VoiceVoxClient.h"
#include "AppConfig.h"

namespace OriGine {
class Microphone;
class WebCamera;
class ScreenCapture;
}

struct SharedMediaContext {
    AppConfigData config;

    OriGine::WebCamera* webCamera = nullptr;
    OriGine::ScreenCapture* screenCapture = nullptr;
    std::vector<uint8_t> camFrameBuffer;
    std::vector<uint8_t> screenFrameBuffer;

    VoiceVoxClient* voiceVox = nullptr;
    std::vector<VoiceVoxSpeaker> voiceVoxSpeakers;
    int selectedSpeaker = 0;
    bool isSpeaking = false;
    std::future<bool> speakFuture;

    std::string transcribedText;

    // 声紋登録用: MicrophonePanel がコピーを書き込む
    std::vector<float> voiceSnapshotBuffer;
    uint32_t voiceSnapshotSampleRate = 16000;
    bool voiceSnapshotReady = false;

    // 顔識別結果 (MemoryPanel が毎フレーム更新)
    std::string identifiedUserName;
    float identifiedSimilarity = 0.0f;
    bool userIdentified = false;

    // ホットキー / ウェイクワード
    bool hotkeyEnabled = true;
    int hotkeyModifiers = 0x0006; // MOD_CONTROL | MOD_SHIFT
    int hotkeyVk = 'L';
    std::string wakeWord = "LAVI";
    bool wakeWordEnabled = true;
};
