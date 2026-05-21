#pragma once

#include <string>

struct AppConfigData {
    std::string apiKey;
    std::string llmSystemPrompt = "あなたはLAVIという名前のAIアシスタントです。日本語で会話してください。";
    std::string visionPrompt = "この画像に写っているものを日本語で説明してください。";
};

AppConfigData LoadAppConfig();
void SaveAppConfig(const AppConfigData& config);
