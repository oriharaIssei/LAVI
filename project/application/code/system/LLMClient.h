#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

struct LLMMessage {
    std::string role; // "user" or "assistant"
    std::string content;

    struct ImageContent {
        std::string base64Data;
        std::string mediaType = "image/jpeg";
    };
    std::vector<ImageContent> images;
};

struct LLMResponse {
    std::string content;
    bool success = false;
    std::string error;
    int inputTokens = 0;
    int outputTokens = 0;
    int cacheCreationInputTokens = 0;
    int cacheReadInputTokens = 0;
};

using LLMStreamCallback = std::function<void(const std::string& delta, bool done)>;

class LLMClient {
public:
    LLMClient();
    ~LLMClient();

    void SetApiKey(const std::string& apiKey);
    void SetModel(const std::string& model);
    void SetSystemPrompt(const std::string& prompt);
    void SetMaxTokens(int maxTokens);

    void AddMessage(const std::string& role, const std::string& content);
    void AddMessageWithImage(const std::string& role, const std::string& text,
                             const uint8_t* bgraData, uint32_t width, uint32_t height);

    struct ImageFrame {
        const uint8_t* data;
        uint32_t width;
        uint32_t height;
    };
    void AddMessageWithImages(const std::string& role, const std::string& text,
                              const std::vector<ImageFrame>& frames);
    void ClearHistory();
    const std::vector<LLMMessage>& GetHistory() const { return history_; }

    LLMResponse SendBlocking();
    std::future<LLMResponse> SendAsync();
    std::future<LLMResponse> SendStreamAsync(LLMStreamCallback callback);

    bool IsProcessing() const { return isProcessing_.load(); }
    void Cancel();

private:
    std::string BuildRequestBody(bool stream) const;
    std::string EncodeToBase64Jpeg(const uint8_t* bgraData, uint32_t width, uint32_t height);
    LLMResponse SendRequest();
    LLMResponse SendStreamRequest(LLMStreamCallback callback);

    static std::string EscapeJson(const std::string& input);
    static std::string ParseResponseContent(const std::string& json);
    static std::string ParseErrorMessage(const std::string& json);
    static void ParseUsage(const std::string& json, LLMResponse& result);

    std::string apiKey_;
    std::string model_ = "claude-haiku-4-5-20251001";
    std::string systemPrompt_;
    int maxTokens_ = 4096;
    std::vector<LLMMessage> history_;

    std::atomic<bool> isProcessing_{false};
    std::atomic<bool> cancelRequested_{false};
    mutable std::mutex mutex_;
};
