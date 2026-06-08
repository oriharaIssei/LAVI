#pragma once

#include <string>

struct AppConfigData {
    std::string apiKey;
    std::string llmSystemPrompt;
    std::string visionPrompt;
    bool minimizeToTrayOnClose = false; // 閉じるボタン(X)でトレイに最小化するか（false=X で終了）

    static const char* DefaultLLMSystemPrompt();
    static const char* DefaultVisionPrompt();
};

AppConfigData LoadAppConfig();
void SaveAppConfig(const AppConfigData& config);
