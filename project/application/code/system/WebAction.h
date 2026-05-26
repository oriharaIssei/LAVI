#pragma once

#include <string>
#include <vector>

class LongTermMemory;

struct WebAction {
    std::string url;
};

// LLM output [browser:URL] tag handling
std::vector<WebAction> ParseWebActions(const std::string& text);
std::string StripWebActions(const std::string& text);
void ExecuteWebActions(const std::vector<WebAction>& actions, LongTermMemory* memory = nullptr);

// LLM system prompt for web action capability
const char* GetWebActionPrompt();

// Tag axes loading (used by LearnInterestsFromSpeech)
bool LoadTagAxes(const std::string& jsonPath);

// Learn user interests from speech content
void LearnInterestsFromSpeech(const std::string& speech, LongTermMemory* memory);

// Legacy: kept for backward compatibility, prefer ActionPipeline
bool ContainsActionKeyword(const std::string& text);

struct LocalActionResult {
    bool handled = false;
    std::string url;
    std::string serviceName;
    std::string query;
    std::string spokenText;
};

class SentenceEmbedding;
LocalActionResult TryLocalAction(const std::string& speech, LongTermMemory* memory,
                                 SentenceEmbedding* embedding = nullptr);
