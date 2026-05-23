#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class OnnxInferenceEngine;

struct FaceProfile {
    std::string name;
    std::vector<float> embedding;
    int sightCount = 0;
    std::string lastSeen;
};

struct VoiceProfile {
    std::string name;
    std::vector<float> mfcc;
    int hearCount = 0;
    std::string lastHeard;
};

struct IdentifyResult {
    bool identified = false;
    std::string name;
    float similarity = 0.0f;
    int profileIndex = -1;
};

class UserIdentifier {
public:
    UserIdentifier();
    ~UserIdentifier();

    bool InitializeFace(const std::wstring& modelPath, bool useGpu = false);
    bool IsFaceModelLoaded() const;

    // BGRA フレームから検出済みの顔領域を渡して特徴量を抽出
    // ArcFace モデルがあればそちらを使用、なければ LBP フォールバック
    std::vector<float> ExtractFaceEmbedding(const uint8_t* bgra, uint32_t width, uint32_t height,
                                             int faceX, int faceY, int faceW, int faceH);

    // PCM 16kHz モノラル音声から声紋 (MFCC) を抽出
    static std::vector<float> ExtractVoiceFeatures(const float* samples, uint32_t sampleCount,
                                                    uint32_t sampleRate = 16000);

    // プロファイル管理
    IdentifyResult IdentifyFace(const std::vector<float>& embedding) const;
    IdentifyResult IdentifyVoice(const std::vector<float>& mfcc) const;

    void RegisterFace(const std::string& name, const std::vector<float>& embedding);
    void RegisterVoice(const std::string& name, const std::vector<float>& mfcc);

    void UpdateFaceEmbedding(int index, const std::vector<float>& embedding, float blendRate = 0.1f);
    void UpdateVoiceMfcc(int index, const std::vector<float>& mfcc, float blendRate = 0.1f);

    const std::vector<FaceProfile>& FaceProfiles() const { return faceProfiles_; }
    const std::vector<VoiceProfile>& VoiceProfiles() const { return voiceProfiles_; }

    void SetFaceThreshold(float t) { faceThreshold_ = t; }
    void SetVoiceThreshold(float t) { voiceThreshold_ = t; }

    bool Load(const std::string& dirPath);
    bool Save(const std::string& dirPath) const;

private:
    static float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);
    static std::vector<float> ExtractLbpEmbedding(const uint8_t* bgra, uint32_t width, uint32_t height,
                                                    int faceX, int faceY, int faceW, int faceH);
    std::vector<float> ExtractArcFaceEmbedding(const uint8_t* bgra, uint32_t width, uint32_t height,
                                                int faceX, int faceY, int faceW, int faceH);
    static std::string GetCurrentTimeString();

    std::unique_ptr<OnnxInferenceEngine> arcface_;
    mutable std::mutex mutex_;

    std::vector<FaceProfile> faceProfiles_;
    std::vector<VoiceProfile> voiceProfiles_;

    float faceThreshold_ = 0.5f;
    float voiceThreshold_ = 0.7f;
};
