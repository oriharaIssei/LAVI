#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct sqlite3;

class InterestGraph {
public:
    InterestGraph();
    ~InterestGraph();

    void Initialize(sqlite3* db, const std::string& taxonomyPath);

    std::string ResolveEntity(const std::string& inputText, const std::string& sourceHint);
    std::string CreateEntity(const std::string& id, const std::string& canonicalName,
                             const std::string& entityType, const std::string& domainWeightsJson,
                             const std::string& source);
    void AddAlias(const std::string& alias, const std::string& entityId);
    void AddRelation(const std::string& fromId, const std::string& toId,
                     const std::string& relType, float weight, const std::string& source);
    void MergeEntities(const std::string& keepId, const std::string& removeId);

    int StartSession();
    void EndSession(int sessionId);
    int GetActiveSessionId() const { return activeSessionId_; }

    void RecordEvent(const std::string& entityId, const std::string& eventType,
                     int durationSec, const std::string& depth, const std::string& source);

    struct EntityInfo {
        std::string id;
        std::string canonicalName;
        std::string entityType;
        int eventCount = 0;
    };
    std::vector<EntityInfo> GetTopEntities(int limit = 20) const;

    struct EventSummary {
        std::string entityId;
        std::string entityName;
        std::string eventType;
        int count = 0;
        int totalDurationSec = 0;
    };
    std::vector<EventSummary> GetRecentEventSummary(int days = 7) const;

    int GetEntityCount() const;
    int GetEventCount() const;

    bool IsValidEntityType(const std::string& type) const;
    bool IsValidEventType(const std::string& type) const;
    bool IsValidRelationType(const std::string& type) const;
    bool IsValidDepth(const std::string& depth) const;

private:
    void InitTables();
    void LoadTaxonomy(const std::string& path);
    static std::string Normalize(const std::string& input);
    std::string Slugify(const std::string& input);
    static std::string HashId(const std::string& input);
    static long long ParseEpochSec(const std::string& iso);
    static std::string MergeDomainWeightsJson(const std::string& existing, const std::string& incoming);
    std::string InferEntityType(const std::string& sourceHint) const;
    std::string InferDomainWeights(const std::string& sourceHint) const;
    std::string GetCurrentTimeISO() const;
    void UpdateDomainWeights(const std::string& entityId, const std::string& sourceHint);
    void ComputeSessionSummary(int sessionId);

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;

    int activeSessionId_ = -1;
    std::string lastEventTime_;
    int sessionIdleTimeoutSec_ = 900;

    std::unordered_set<std::string> validEntityTypes_;
    std::unordered_set<std::string> validEventTypes_;
    std::unordered_set<std::string> validRelationTypes_;
    std::unordered_set<std::string> validDepths_;

    std::unordered_map<std::string, std::string> aliasCache_;
};
