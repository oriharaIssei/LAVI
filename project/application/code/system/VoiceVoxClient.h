#pragma once

#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>

struct VoiceVoxSpeaker {
    int id;
    std::string name;
    std::string styleName;
};

class VoiceVoxClient {
public:
    VoiceVoxClient();
    ~VoiceVoxClient();

    bool StartEngine(const std::string& enginePath);
    void StopEngine();
    bool IsEngineRunning() const;
    bool IsEngineReady() const;

    std::vector<VoiceVoxSpeaker> GetSpeakers();

    std::future<bool> SpeakAsync(const std::string& text, int speakerId);
    bool Speak(const std::string& text, int speakerId);

    bool IsPlaying() const;
    void Stop();

    void SetBaseUrl(const std::string& url) { baseUrl_ = url; }

private:
    std::vector<uint8_t> Synthesize(const std::string& text, int speakerId);
    std::string HttpGet(const std::string& url);
    std::string HttpPost(const std::string& url, const std::string& body, const std::string& contentType);
    bool PlayWav(const std::vector<uint8_t>& wavData);

    bool InitXAudio2();
    void ShutdownXAudio2();

    std::string baseUrl_ = "http://127.0.0.1:50021";
    HANDLE engineProcess_ = nullptr;

    IXAudio2* xaudio2_ = nullptr;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;
    IXAudio2SourceVoice* sourceVoice_ = nullptr;
    std::vector<uint8_t> playbackBuffer_;
    std::atomic<bool> isPlaying_{false};

    mutable std::mutex mutex_;
};
