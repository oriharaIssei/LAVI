#pragma once

#include <chrono>
#include <future>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LLMClient.h"

class InterestGraph;
class LocalLLM;
class LongTermMemory;

class AppUsageTracker {
public:
    AppUsageTracker();
    ~AppUsageTracker();

    void Initialize(LongTermMemory* ltm, LocalLLM* llm, const std::string& apiKey,
                    InterestGraph* ig = nullptr);
    void Update(float deltaTime);
    void Finalize();

    float GetScanInterval() const { return scanInterval_; }
    void SetScanInterval(float sec) { scanInterval_ = sec; }
    bool IsTagging() const;

    struct ActiveAppInfo {
        std::string processName;
        std::string windowTitle;
        bool isForeground = false;
        float sessionMinutes = 0.0f;
    };
    std::vector<ActiveAppInfo> GetActiveApps() const;

private:
    struct TrackedApp {
        std::string processName;
        std::string windowTitle;
        std::chrono::steady_clock::time_point openTime;
        std::chrono::steady_clock::time_point segmentStart;
        float accumulatedFgMinutes = 0.0f;
        float accumulatedBgMinutes = 0.0f;
        bool isForeground = false;
    };

    enum class TagStage { None, LocalLLM, CloudAPI };

    struct PendingTag {
        std::string processName;
        std::string windowTitle;
    };

    void ScanProcesses();
    void CloseSegment(TrackedApp& app);
    void FlushApp(TrackedApp& app);
    void DetectWebService(const std::string& browserProcess, const std::string& windowTitle);
    static bool IsBrowser(const std::string& processName);
    static std::string ExtractServiceName(const std::string& windowTitle);
    void EnqueueTagging(const std::string& processName, const std::string& windowTitle);
    void StartNextTagging();
    void PollTagging();
    std::vector<std::string> ParseTags(const std::string& response) const;
    bool IsUnknownResponse(const std::string& response) const;
    static std::string BuildLocalTagPrompt(const std::string& processName, const std::string& windowTitle);
    static std::string BuildCloudTagPrompt(const std::string& processName, const std::string& windowTitle);

    LongTermMemory* ltm_ = nullptr;
    LocalLLM* llm_ = nullptr;
    InterestGraph* ig_ = nullptr;

    float scanInterval_ = 3.0f;
    float scanTimer_ = 0.0f;

    std::string currentForeground_;
    std::unordered_map<std::string, TrackedApp> runningApps_;
    std::unordered_set<std::string> everSeenApps_;

    std::string currentWebService_;
    std::chrono::steady_clock::time_point webServiceStart_;

    // Parallel session detection
    float parallelScanInterval_ = 60.0f;
    float parallelScanTimer_ = 0.0f;
    struct ParallelSnapshot {
        std::string foregroundApp;
        std::string foregroundTitle;
        std::vector<std::pair<std::string, std::string>> backgroundApps;
        std::chrono::steady_clock::time_point timestamp;
    };
    ParallelSnapshot lastSnapshot_;
    void DetectParallelUsage();
    static std::string ClassifyContext(const std::string& processName, const std::string& windowTitle);

    // Hybrid tagging
    TagStage tagStage_ = TagStage::None;
    std::string pendingTagProcess_;
    std::string pendingTagTitle_;
    std::future<std::string> localTagFuture_;
    LLMClient cloudLlm_;
    std::future<LLMResponse> cloudTagFuture_;
    std::vector<PendingTag> tagQueue_;
};
