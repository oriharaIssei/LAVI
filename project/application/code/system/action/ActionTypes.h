#pragma once

#include <string>
#include <vector>

// --- Intent Layer ---

enum class IntentType {
    None,

    PlayMusic,
    SearchWeb,
    OpenWebsite,
    OpenApplication,

    Unknown
};

struct IntentResult {
    IntentType type = IntentType::None;
    std::string rawText;
    std::string query;
    std::string targetName;
    float confidence = 0.0f;
};

// --- Capability Layer ---

enum class CapabilityType {
    MusicPlayback,
    BrowserSearch,
    BrowserOpen,
    ApplicationLaunch
};

struct CapabilityResult {
    CapabilityType type = CapabilityType::BrowserSearch;
};

// --- Action Layer ---

enum class ActionType {
    OpenUrl,
    SearchUrl,
    LaunchApplication
    // TODO: GuiClick, GuiType, Wait, OcrVerify
};

struct ActionCommand {
    ActionType type = ActionType::OpenUrl;
    std::string parameter;
    std::string browserProcess;
    std::string label;
};

// --- Context ---

struct UserPreference {
    std::string preferredMusicService;
    std::string preferredSearchService;
    std::string preferredBrowser;
    // TODO: time-based, activity-based preferences
};

// --- Pipeline Result ---

struct ActionResult {
    bool handled = false;
    std::string url;
    std::string serviceName;
    std::string query;
    std::string spokenText;
};
