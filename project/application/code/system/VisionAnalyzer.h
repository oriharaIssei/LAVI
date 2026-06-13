#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

struct VisionResult {
    std::string description;
    bool success = false;
    std::string error;
};

class VisionAnalyzer {
public:
    VisionAnalyzer();
    ~VisionAnalyzer();

    void SetApiKey(const std::string& apiKey);
    void SetModel(const std::string& model);
    void SetPrompt(const std::string& prompt);

    std::future<VisionResult> AnalyzeAsync(const uint8_t* bgraData, uint32_t width, uint32_t height);
    VisionResult Analyze(const uint8_t* bgraData, uint32_t width, uint32_t height);

    // 複数フレーム（BGRA）を 1 リクエストにまとめて送る（「動画的解釈」用の時系列フレーム）。
    struct Frame {
        const uint8_t* bgra;
        uint32_t width;
        uint32_t height;
    };
    std::future<VisionResult> AnalyzeAsync(const std::vector<Frame>& frames);

    bool IsAnalyzing() const { return isAnalyzing_.load(); }

private:
    std::string EncodeToBase64Jpeg(const uint8_t* bgraData, uint32_t width, uint32_t height);
    VisionResult SendRequest(const std::string& base64Image);
    VisionResult SendRequestMulti(const std::vector<std::string>& base64Images);

    std::string apiKey_;
    std::string model_ = "claude-sonnet-4-20250514";
    std::string prompt_ = "この画像に写っているものを日本語で説明してください。";
    std::atomic<bool> isAnalyzing_{false};
    mutable std::mutex mutex_;
};
