#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

class LocalLLM;

struct MemoryEntry {
    std::string role;
    std::string content;
    std::chrono::steady_clock::time_point timestamp;
    bool hasImage = false;
};

class ConversationMemory {
public:
    ConversationMemory();
    ~ConversationMemory();

    void Initialize(LocalLLM* localLLM);

    void PushMessage(const std::string& role, const std::string& content, bool hasImage = false);

    std::string GetSummary() const;
    const std::vector<MemoryEntry>& GetRecentEntries() const { return recentEntries_; }

    void SetMaxRecentEntries(int maxEntries);
    void SetMaxSummaryTokens(int maxTokens);

    void Update();

    bool IsSummarizing() const { return isSummarizing_; }
    void Clear();

    struct Stats {
        int totalMessages = 0;
        int recentMessages = 0;
        int summarizedMessages = 0;
        int estimatedTokens = 0;
    };
    Stats GetStats() const;

private:
    void TriggerSummarization();
    int EstimateTokens(const std::string& text) const;

    LocalLLM* localLLM_ = nullptr;

    mutable std::mutex mutex_;
    std::vector<MemoryEntry> recentEntries_;
    std::string summary_;
    int totalMessageCount_ = 0;
    int summarizedCount_ = 0;

    int maxRecentEntries_ = 20;
    int maxSummaryTokens_ = 512;

    std::future<std::string> summarizeFuture_;
    bool isSummarizing_ = false;
    std::vector<MemoryEntry> pendingSummarize_;
};
