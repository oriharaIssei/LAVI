#pragma once

#include "ActionTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

class LongTermMemory;

class MemoryResolver {
public:
    UserPreference ResolvePreferences(LongTermMemory* memory) const;

    std::vector<std::string> ResolveQueryKeywords(
        const IntentResult& intent,
        const std::vector<std::string>& interestCategories,
        const std::unordered_map<std::string, std::string>& synonyms,
        LongTermMemory* memory) const;

    std::string ResolveService(
        const CapabilityResult& capability,
        const IntentResult& intent,
        const UserPreference& preference,
        LongTermMemory* memory) const;
};
