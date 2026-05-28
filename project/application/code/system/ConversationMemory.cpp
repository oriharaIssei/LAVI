#include "ConversationMemory.h"
#include "LocalLLM.h"

#include <fstream>
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

namespace {

std::string EscapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '"':  result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:   result += c; break;
        }
    }
    return result;
}

std::string UnescapeJson(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
            case '"':  result += '"'; ++i; break;
            case '\\': result += '\\'; ++i; break;
            case 'n':  result += '\n'; ++i; break;
            case 'r':  result += '\r'; ++i; break;
            case 't':  result += '\t'; ++i; break;
            default:   result += s[i]; break;
            }
        } else {
            result += s[i];
        }
    }
    return result;
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    std::string result;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            result += json[i];
            result += json[i + 1];
            ++i;
        } else if (json[i] == '"') {
            break;
        } else {
            result += json[i];
        }
    }
    return UnescapeJson(result);
}

int ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    return std::atoi(json.c_str() + pos + search.size());
}

}  // namespace

bool ConversationMemory::Save(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream out;
    out << "{\n";
    out << "  \"summary\":\"" << EscapeJson(summary_) << "\",\n";
    out << "  \"totalMessages\":" << totalMessageCount_ << ",\n";
    out << "  \"summarizedCount\":" << summarizedCount_ << ",\n";
    out << "  \"entries\":[\n";
    for (size_t i = 0; i < recentEntries_.size(); ++i) {
        auto& e = recentEntries_[i];
        out << "    {\"role\":\"" << EscapeJson(e.role) << "\","
            << "\"content\":\"" << EscapeJson(e.content) << "\","
            << "\"hasImage\":" << (e.hasImage ? "true" : "false") << "}";
        if (i + 1 < recentEntries_.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << out.str();
    return true;
}

bool ConversationMemory::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string json((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    if (json.empty()) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    summary_ = ExtractJsonString(json, "summary");
    totalMessageCount_ = ExtractJsonInt(json, "totalMessages");
    summarizedCount_ = ExtractJsonInt(json, "summarizedCount");

    recentEntries_.clear();
    auto arrPos = json.find("\"entries\":[");
    if (arrPos != std::string::npos) {
        size_t pos = arrPos;
        while (true) {
            auto objStart = json.find("{\"role\":", pos);
            if (objStart == std::string::npos) break;

            auto objEnd = json.find("}", objStart);
            if (objEnd == std::string::npos) break;

            std::string obj = json.substr(objStart, objEnd - objStart + 1);

            MemoryEntry entry;
            entry.role = ExtractJsonString(obj, "role");
            entry.content = ExtractJsonString(obj, "content");
            entry.hasImage = (obj.find("\"hasImage\":true") != std::string::npos);
            entry.timestamp = std::chrono::steady_clock::now();

            if (!entry.role.empty() && !entry.content.empty()) {
                recentEntries_.push_back(std::move(entry));
            }
            pos = objEnd + 1;
        }
    }

    return true;
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
