#include "MemoryResolver.h"
#include "system/LongTermMemory.h"

UserPreference MemoryResolver::ResolvePreferences(LongTermMemory* memory) const {
    UserPreference pref;
    pref.preferredSearchService = "Google";

    if (!memory) return pref;

    int bestMusicVisit = 0;
    for (auto& ws : memory->GetWebServiceRecords()) {
        bool isMusic = (ws.serviceName == "YouTube" ||
                        ws.serviceName == "Spotify" ||
                        ws.serviceName == "niconico");
        if (!isMusic) {
            for (auto& t : ws.tags) {
                if (t == "\xE9\x9F\xB3\xE6\xA5\xBD") { isMusic = true; break; }
            }
        }
        if (isMusic && ws.visitCount > bestMusicVisit) {
            bestMusicVisit = ws.visitCount;
            pref.preferredMusicService = ws.serviceName;
        }

        if (!ws.browserProcess.empty() && pref.preferredBrowser.empty()) {
            pref.preferredBrowser = ws.browserProcess;
        }
    }

    return pref;
}

std::vector<std::string> MemoryResolver::ResolveQueryKeywords(
    const IntentResult& intent,
    const std::vector<std::string>& interestCategories,
    const std::unordered_map<std::string, std::string>& synonyms,
    LongTermMemory* memory) const {

    if (!memory) return {};

    std::vector<std::string> matchedCategories;
    auto addCat = [&](const std::string& cat) {
        for (auto& m : matchedCategories) if (m == cat) return;
        matchedCategories.push_back(cat);
    };

    for (auto& cat : interestCategories) {
        if (intent.rawText.find(cat) != std::string::npos) addCat(cat);
    }
    for (auto& [word, tag] : synonyms) {
        if (intent.rawText.find(word) != std::string::npos) {
            for (auto& cat : interestCategories) {
                if (tag == cat) { addCat(cat); break; }
            }
        }
    }

    std::vector<std::string> keywords;
    if (!matchedCategories.empty()) {
        for (auto& cat : matchedCategories) {
            auto interests = memory->GetInterests(cat, 3);
            for (auto& kw : interests) {
                if (kw.size() >= 2) keywords.push_back(kw);
            }
        }
    }

    if (keywords.empty() && intent.query.size() >= 2) {
        keywords.push_back(intent.query);
    }

    return keywords;
}

std::string MemoryResolver::ResolveService(
    const CapabilityResult& capability,
    const IntentResult& intent,
    const UserPreference& preference,
    LongTermMemory* memory) const {

    if (!intent.targetName.empty()) return intent.targetName;

    switch (capability.type) {
    case CapabilityType::MusicPlayback:
        if (!preference.preferredMusicService.empty())
            return preference.preferredMusicService;
        return "YouTube";

    case CapabilityType::BrowserSearch:
        if (!preference.preferredSearchService.empty())
            return preference.preferredSearchService;
        return "Google";

    case CapabilityType::BrowserOpen: {
        if (!memory) return "Google";
        int bestScore = 0;
        std::string bestService = "Google";
        for (auto& ws : memory->GetWebServiceRecords()) {
            if (ws.visitCount > bestScore) {
                bestScore = ws.visitCount;
                bestService = ws.serviceName;
            }
        }
        return bestService;
    }

    case CapabilityType::ApplicationLaunch:
        return "";

    default:
        return "Google";
    }
}
