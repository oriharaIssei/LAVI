#pragma once

#include <string>
#include <vector>

class LongTermMemory;
class SentenceEmbedding;

struct WebAction {
    std::string url;
};

std::vector<WebAction> ParseWebActions(const std::string& text);
std::string StripWebActions(const std::string& text);
void ExecuteWebActions(const std::vector<WebAction>& actions, LongTermMemory* memory = nullptr);

const char* GetWebActionPrompt();

struct LocalActionResult {
    bool handled = false;
    std::string url;
    std::string serviceName;
    std::string query;
    std::string spokenText;
};

LocalActionResult TryLocalAction(const std::string& speech, LongTermMemory* memory,
                                 SentenceEmbedding* embedding = nullptr);

bool ContainsActionKeyword(const std::string& text);

bool LoadTagAxes(const std::string& jsonPath);

void LearnInterestsFromSpeech(const std::string& speech, LongTermMemory* memory);
