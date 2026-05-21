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
};
