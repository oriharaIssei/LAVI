#pragma once

#include "ActionTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

class IntentExtractor {
public:
    bool LoadConfig(const std::string& tagAxesPath);

    IntentResult Extract(const std::string& speech) const;
    bool ContainsActionKeyword(const std::string& text) const;

    const std::vector<std::string>& GetInterestCategories() const { return interestCategories_; }
    const std::unordered_map<std::string, std::string>& GetSynonyms() const { return synonyms_; }

private:
    struct IntentRule {
        IntentType type;
        std::vector<std::string> keywords;
    };
    std::vector<IntentRule> rules_;

    std::vector<std::string> interestCategories_;
    std::vector<std::string> allTags_;
    std::unordered_map<std::string, std::string> synonyms_;

    bool TryExtractApplication(const std::string& speech, IntentResult& result) const;
    std::string ExtractSubject(const std::string& speech) const;
    void BuildRules();
};
