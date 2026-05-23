#include "ConversationMemory.h"
#include "LocalLLM.h"

#include <sstream>

static const char* kSummarizePrompt =
    "以下の会話ログを短く要約してください。重要な情報（ユーザーの発言内容、話題、感情、決定事項）を保持し、"
    "不要な挨拶や繰り返しは省略してください。要約は日本語で、箇条書きで出力してください。\n\n"
    "### 会話ログ\n";

ConversationMemory::ConversationMemory() {}

ConversationMemory::~ConversationMemory() {
    if (summarizeFuture_.valid()) {
        summarizeFuture_.wait();
    }
}

void ConversationMemory::Initialize(LocalLLM* localLLM) {
    localLLM_ = localLLM;
}

void ConversationMemory::PushMessage(const std::string& role, const std::string& content, bool hasImage) {
    std::lock_guard<std::mutex> lock(mutex_);

    MemoryEntry entry;
    entry.role = role;
    entry.content = content;
    entry.timestamp = std::chrono::steady_clock::now();
    entry.hasImage = hasImage;

    recentEntries_.push_back(std::move(entry));
    ++totalMessageCount_;

    if (static_cast<int>(recentEntries_.size()) > maxRecentEntries_ * 2 && !isSummarizing_) {
        TriggerSummarization();
    }
}

std::string ConversationMemory::GetSummary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return summary_;
}

void ConversationMemory::SetMaxRecentEntries(int maxEntries) {
    maxRecentEntries_ = maxEntries;
}

void ConversationMemory::SetMaxSummaryTokens(int maxTokens) {
    maxSummaryTokens_ = maxTokens;
}

void ConversationMemory::Update() {
    if (!isSummarizing_) return;
    if (!summarizeFuture_.valid()) return;

    if (summarizeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::string newSummary = summarizeFuture_.get();

        std::lock_guard<std::mutex> lock(mutex_);
        if (!newSummary.empty()) {
            if (!summary_.empty()) {
                summary_ += "\n";
            }
            summary_ += newSummary;

            int summaryTokens = EstimateTokens(summary_);
            if (summaryTokens > maxSummaryTokens_ && localLLM_ && localLLM_->IsModelLoaded()) {
                std::string compressPrompt =
                    "以下の要約を更に短く圧縮してください。最も重要な情報だけを残してください。\n\n" + summary_;
                summary_ = localLLM_->Generate(compressPrompt);
            }
        }

        summarizedCount_ += static_cast<int>(pendingSummarize_.size());
        pendingSummarize_.clear();
        isSummarizing_ = false;
    }
}

void ConversationMemory::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    recentEntries_.clear();
    summary_.clear();
    totalMessageCount_ = 0;
    summarizedCount_ = 0;
}

ConversationMemory::Stats ConversationMemory::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s;
    s.totalMessages = totalMessageCount_;
    s.recentMessages = static_cast<int>(recentEntries_.size());
    s.summarizedMessages = summarizedCount_;

    int tokens = EstimateTokens(summary_);
    for (auto& e : recentEntries_) {
        tokens += EstimateTokens(e.content);
    }
    s.estimatedTokens = tokens;
    return s;
}

void ConversationMemory::TriggerSummarization() {
    if (!localLLM_ || !localLLM_->IsModelLoaded()) return;

    int entriesToSummarize = static_cast<int>(recentEntries_.size()) - maxRecentEntries_;
    if (entriesToSummarize <= 0) return;

    pendingSummarize_.assign(recentEntries_.begin(), recentEntries_.begin() + entriesToSummarize);
    recentEntries_.erase(recentEntries_.begin(), recentEntries_.begin() + entriesToSummarize);

    std::ostringstream logBuilder;
    for (auto& e : pendingSummarize_) {
        std::string roleLabel = (e.role == "user") ? "User" : "LAVI";
        logBuilder << roleLabel << ": " << e.content;
        if (e.hasImage) {
            logBuilder << " [画像添付]";
        }
        logBuilder << "\n";
    }

    std::string prompt = std::string(kSummarizePrompt) + logBuilder.str();
    localLLM_->SetMaxTokens(maxSummaryTokens_);

    isSummarizing_ = true;
    summarizeFuture_ = localLLM_->GenerateAsync(prompt);
}

int ConversationMemory::EstimateTokens(const std::string& text) const {
    // Japanese text: ~1.5 characters per token on average
    int charCount = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            ++charCount;
            i += 1;
        } else if (c < 0xE0) {
            ++charCount;
            i += 2;
        } else if (c < 0xF0) {
            ++charCount;
            i += 3;
        } else {
            ++charCount;
            i += 4;
        }
    }
    return static_cast<int>(charCount / 1.5f + 0.5f);
}
