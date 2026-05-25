#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class LocalLLM;
class LongTermMemory;

struct BrowsingEntry {
    std::string url;
    std::string title;
    int visitCount = 0;
    std::string lastVisit;
    std::string browserName;
};

struct BrowserCollectionInfo {
    std::string name;
    bool found = false;
    int entryCount = 0;
};

struct CollectionReport {
    std::vector<BrowserCollectionInfo> browsers;
    int totalEntries = 0;
    int servicesFound = 0;
    std::vector<std::string> extractedKeywords;
    std::vector<std::string> rejectedLines;
    std::string llmRawOutput;
    std::string phase;
    int categoriesLoaded = 0;
};

class BrowsingHistoryCollector {
public:
    BrowsingHistoryCollector();
    ~BrowsingHistoryCollector();

    void LoadCategories(const std::string& tagAxesPath);
    const std::vector<std::string>& GetCategories() const { return categories_; }
    bool IsValidCategory(const std::string& cat) const;

    std::vector<BrowsingEntry> ReadRecentHistory(int maxEntries = 200, int daysBack = 7) const;
    const std::vector<BrowsingEntry>& LastRawEntries() const { return lastRawEntries_; }

    void CollectInterests(LongTermMemory* memory, LocalLLM* llm);
    void CollectServiceKeywords(LongTermMemory* memory, LocalLLM* llm, int daysBack = 30);

    bool IsCollecting() const { return isCollecting_; }
    const std::string& LastError() const { return lastError_; }
    int LastCollectedCount() const { return lastCollectedCount_; }
    const CollectionReport& LastReport() const { return lastReport_; }

private:
    struct BrowserProfile {
        std::string name;
        std::string historyPath;
    };

    std::vector<BrowserProfile> FindBrowserProfiles() const;
    std::vector<BrowsingEntry> ReadSqliteHistory(const std::string& dbPath,
                                                  int maxEntries, int daysBack) const;
    std::string BuildTitleList(const std::vector<BrowsingEntry>& entries) const;

    static std::string ExtractServiceFromUrl(const std::string& url);
    using ServiceGroup = std::unordered_map<std::string, std::vector<BrowsingEntry>>;
    ServiceGroup GroupByService(const std::vector<BrowsingEntry>& entries) const;

    mutable std::mutex mutex_;
    bool isCollecting_ = false;
    std::string lastError_;
    int lastCollectedCount_ = 0;
    mutable CollectionReport lastReport_;
    mutable std::vector<BrowsingEntry> lastRawEntries_;
    std::vector<std::string> categories_;
};
