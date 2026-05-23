#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

struct UserProfile {
    std::string name;
    std::vector<std::string> interests;
    std::vector<std::string> dislikedTopics;
    std::string notes;
};

struct AppDailyStat {
    std::string processName;
    std::string date;
    int foregroundCount = 0;
    float foregroundMinutes = 0.0f;
    int backgroundCount = 0;
    float backgroundMinutes = 0.0f;
};

struct AppUsageRecord {
    std::string processName;
    std::string windowTitle;
    std::vector<std::string> tags;
    int launchCount = 0;
    float totalForegroundMinutes = 0.0f;
    float totalBackgroundMinutes = 0.0f;
    std::string firstSeen;
    std::string lastSeen;
};

struct WebServiceRecord {
    std::string serviceName;
    std::string browserProcess;
    std::vector<std::string> tags;
    float totalMinutes = 0.0f;
    int visitCount = 0;
    std::string firstVisited;
    std::string lastVisited;
};

struct AppLaunchRecord {
    std::string launchedApp;
    std::string launchedFrom;
    int count = 0;
    std::string lastTime;
};

struct AppTransition {
    std::string fromApp;
    std::string toApp;
    int count = 0;
    std::string lastTime;
};

struct MemoryFact {
    std::string category;
    std::string content;
    std::string learnedAt;
    float importance = 1.0f;
};

class LongTermMemory {
public:
    LongTermMemory();
    ~LongTermMemory();

    void SetStoragePath(const std::string& dirPath);
    bool Load();
    bool Save() const;

    UserProfile& GetUserProfile() { return userProfile_; }
    const UserProfile& GetUserProfile() const { return userProfile_; }

    void RecordAppForeground(const std::string& processName, const std::string& windowTitle, float durationMinutes);
    void RecordAppBackground(const std::string& processName, const std::string& windowTitle, float durationMinutes);
    void SetAppTags(const std::string& processName, const std::vector<std::string>& tags);
    AppUsageRecord* FindAppRecord(const std::string& processName);
    const std::vector<AppUsageRecord>& GetAppUsageRecords() const { return appUsage_; }
    std::vector<AppDailyStat> GetDailyStats(int limit = 100) const;

    void RecordAppTransition(const std::string& fromApp, const std::string& toApp);
    std::vector<AppTransition> GetAppTransitions(int limit = 0) const;

    void RecordWebService(const std::string& serviceName, const std::string& browserProcess, float durationMinutes);
    void SetWebServiceTags(const std::string& serviceName, const std::vector<std::string>& tags);
    WebServiceRecord* FindWebService(const std::string& serviceName);
    const std::vector<WebServiceRecord>& GetWebServiceRecords() const { return webServices_; }

    void RecordAppLaunch(const std::string& launchedApp, const std::string& launchedFrom);
    std::vector<AppLaunchRecord> GetAppLaunchRecords(int limit = 0) const;

    void AddFact(const std::string& category, const std::string& content, float importance = 1.0f);
    std::vector<MemoryFact> GetFacts(const std::string& category = "") const;
    int GetFactCount() const;

    std::string BuildContextString() const;
    std::string BuildContextString(const std::string& query) const;
    std::vector<MemoryFact> SearchFacts(const std::string& query, int maxResults = 10) const;

private:
    bool LoadJson(const std::string& filePath, std::string& outContent) const;
    bool SaveJson(const std::string& filePath, const std::string& content) const;
    std::string GetCurrentTimeString() const;

    bool InitDatabase();
    void MigrateJsonToSqlite();
    void UpsertDailyStat(const std::string& processName, const std::string& date,
                         float fgMinutes, float bgMinutes, bool isForeground);
    void ExecSql(const char* sql) const;

    mutable std::mutex mutex_;
    std::string storagePath_;
    sqlite3* db_ = nullptr;

    UserProfile userProfile_;
    std::vector<AppUsageRecord> appUsage_;
    std::vector<WebServiceRecord> webServices_;
};
