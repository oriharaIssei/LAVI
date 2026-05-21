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

struct VoiceVoxSynthParams {
    float speedScale = 1.0f;
    float pitchScale = 0.0f;
    float intonationScale = 1.0f;
    float volumeScale = 1.0f;
    float pauseScale = 1.0f;
};

class VoiceVoxClient {
public:
    VoiceVoxClient();
    ~VoiceVoxClient();

    bool StartEngine(const std::string& enginePath);
    void StopEngine();
    bool IsEngineRunning() const;
    bool IsEngineReady() const;
    const std::string& GetLastErrorMessage() const { return lastError_; }
    DWORD GetEngineExitCode() const;

    std::vector<VoiceVoxSpeaker> GetSpeakers();

    std::future<bool> SpeakAsync(const std::string& text, int speakerId);
    bool Speak(const std::string& text, int speakerId);

    std::vector<uint8_t> SynthesizeWav(const std::string& text, int speakerId);
    std::vector<uint8_t> SynthesizeWav(const std::string& text, int speakerId, const VoiceVoxSynthParams& params);
    bool PlayWavData(const std::vector<uint8_t>& wavData);
    std::future<bool> PlayWavDataAsync(std::vector<uint8_t> wavData);

    bool IsPlaying() const;
    void Stop();

    void SetBaseUrl(const std::string& url) { baseUrl_ = url; }

private:
    std::vector<uint8_t> Synthesize(const std::string& text, int speakerId, const VoiceVoxSynthParams* params = nullptr);
    static void ApplySynthParams(std::string& queryJson, const VoiceVoxSynthParams& params);
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
    std::string lastError_;
    bool externalEngine_ = false;
};
