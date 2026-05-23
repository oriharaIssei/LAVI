#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

class LocalLLM;
class ConversationMemory;
class LongTermMemory;
class BrowsingHistoryCollector;
class UserIdentifier;
class AppUsageTracker;
class GatekeeperManager;

struct SharedMediaContext;

class MemoryPanel {
public:
    MemoryPanel();
    ~MemoryPanel();

    void Initialize(SharedMediaContext* ctx, GatekeeperManager* gkManager = nullptr);
    void Finalize();
    void Update();
    void Draw();

    LocalLLM* GetLocalLLM() { return localLLM_.get(); }
    ConversationMemory* GetConversationMemory() { return conversationMemory_.get(); }
    LongTermMemory* GetLongTermMemory() { return longTermMemory_.get(); }
    UserIdentifier* GetUserIdentifier() { return userIdentifier_.get(); }

    std::string BuildMemoryContext() const;
    void NotifyUserMessage(const std::string& message);

private:
    SharedMediaContext* ctx_ = nullptr;

    std::unique_ptr<LocalLLM> localLLM_;
    std::unique_ptr<ConversationMemory> conversationMemory_;
    std::unique_ptr<LongTermMemory> longTermMemory_;
    std::unique_ptr<BrowsingHistoryCollector> browsingCollector_;
    std::unique_ptr<UserIdentifier> userIdentifier_;
    std::unique_ptr<AppUsageTracker> appTracker_;

    std::string localModelPath_;
    std::string arcfaceModelPath_;
    bool modelLoadRequested_ = false;

    float autoSaveInterval_ = 300.0f;
    float autoSaveTimer_ = 0.0f;

    float browsingCollectInterval_ = 3600.0f;
    float browsingCollectTimer_ = 0.0f;
    bool browsingCollectRequested_ = false;
    bool serviceKeywordRequested_ = false;
    std::future<void> browsingFuture_;

    GatekeeperManager* gkManager_ = nullptr;
    float identifyInterval_ = 0.5f;
    float identifyTimer_ = 0.0f;

    char registerNameBuf_[64] = {};

    // 常時リスニング → メモリ自動投入
    std::string lastTranscribedText_;

    // 会話からの自動事実抽出
    int messagesSinceLastExtraction_ = 0;
    int factExtractionThreshold_ = 6;
    std::future<std::string> factExtractionFuture_;
    bool factExtractionActive_ = false;
    void TriggerFactExtraction();
    void PollFactExtraction();
    static std::string BuildFactExtractionPrompt(const std::vector<std::string>& recentMessages);

    // 文脈ベース記憶検索
    std::string lastUserMessage_;
};
