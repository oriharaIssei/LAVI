#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct whisper_context;

struct WhisperToken {
    std::string text;
    float probability;
    float voiceLength;
    int64_t t0;
    int64_t t1;
};

struct WhisperSegment {
    std::string text;
    int64_t t0;
    int64_t t1;
    std::vector<WhisperToken> tokens;
};

struct WhisperResult {
    std::string fullText;
    std::vector<WhisperSegment> segments;
};

class WhisperTranscriber {
public:
    WhisperTranscriber();
    ~WhisperTranscriber();

    bool LoadModel(const std::string& modelPath, int nThreads = 0);
    void UnloadModel();
    bool IsModelLoaded() const;

    void SetLanguage(const std::string& lang);
    void SetInitialPrompt(const std::string& prompt);
    void SetBeamSize(int beamSize);
    void SetVadModelPath(const std::string& path);

    void PushAudio(const float* samples, uint32_t frameCount, uint32_t channels, uint32_t sampleRate);

    bool Transcribe();

    std::string GetResult() const;
    WhisperResult GetDetailedResult() const;

    void ClearAudio();

    size_t GetAudioSampleCount() const;

private:
    void Resample(const float* src, uint32_t srcFrames, uint32_t srcRate, std::vector<float>& dst);
    void MixToMono(const float* src, uint32_t frameCount, uint32_t channels, std::vector<float>& dst);

    whisper_context* ctx_ = nullptr;
    int nThreads_ = 4;
    std::string language_ = "ja";
    std::string initialPrompt_;
    int beamSize_ = 5;
    std::string vadModelPath_;

    mutable std::mutex audioMutex_;
    std::vector<float> audioBuffer_;

    mutable std::mutex resultMutex_;
    WhisperResult result_;
};
