#pragma once

#include <string>

struct AppConfigData {
    std::string apiKey;
    std::string llmSystemPrompt;
    std::string visionPrompt;

    static const char* DefaultLLMSystemPrompt();
    static const char* DefaultVisionPrompt();
};

AppConfigData LoadAppConfig();
void SaveAppConfig(const AppConfigData& config);
