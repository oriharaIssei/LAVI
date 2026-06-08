#pragma once

#include <future>
#include <memory>
#include <string>
#include <vector>

class InterestGraph;
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

    void Initialize(SharedMediaContext* ctx);
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
    std::unique_ptr<InterestGraph> interestGraph_;

    std::string localModelPath_;
    std::string arcfaceModelPath_;
    bool modelLoadRequested_ = false;

    // ローカル LLM モデルの選択（gguf 一覧 + 選択の永続化。ハードコードしない）
    std::string modelsDir_ = "application/resource/llm/models";
    std::string llmConfigPath_ = "application/resource/llm/llm_config.json";
    std::vector<std::string> availableModels_;
    void ScanModels();        // modelsDir_ の *.gguf を列挙
    void LoadLLMConfig();     // 選択済みモデルパスを復元
    void SaveLLMConfig();     // 選択済みモデルパスを保存

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

    // 既存データ → InterestGraph マイグレーション
    void MigrateToInterestGraph();
};
