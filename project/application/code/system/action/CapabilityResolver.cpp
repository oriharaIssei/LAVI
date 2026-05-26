#include "CapabilityResolver.h"

CapabilityResult CapabilityResolver::Resolve(const IntentResult& intent) const {
    CapabilityResult result;

    switch (intent.type) {
    case IntentType::PlayMusic:
        result.type = CapabilityType::MusicPlayback;
        break;
    case IntentType::SearchWeb:
        result.type = CapabilityType::BrowserSearch;
        break;
    case IntentType::OpenWebsite:
        result.type = CapabilityType::BrowserOpen;
        break;
    case IntentType::OpenApplication:
        result.type = CapabilityType::ApplicationLaunch;
        break;
    default:
        result.type = CapabilityType::BrowserSearch;
        break;
    }

    return result;
}
