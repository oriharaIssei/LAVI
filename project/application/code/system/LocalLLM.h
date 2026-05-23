#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <string>

struct llama_model;
struct llama_context;

using LocalLLMCallback = std::function<void(const std::string& token)>;

class LocalLLM {
public:
    LocalLLM();
    ~LocalLLM();

    bool LoadModel(const std::string& modelPath, int nGpuLayers = 99, int contextSize = 4096);
    void UnloadModel();
    bool IsModelLoaded() const;

    void SetMaxTokens(int maxTokens);

    std::string Generate(const std::string& prompt);
    std::future<std::string> GenerateAsync(const std::string& prompt);
    std::future<std::string> GenerateAsync(const std::string& prompt, LocalLLMCallback callback);

    bool IsProcessing() const { return isProcessing_.load(); }
    void Cancel();

private:
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    int contextSize_ = 4096;
    int maxTokens_ = 512;

    std::atomic<bool> isProcessing_{false};
    std::atomic<bool> cancelRequested_{false};
    mutable std::mutex mutex_;
};
