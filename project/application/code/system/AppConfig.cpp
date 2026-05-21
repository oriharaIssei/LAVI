#include "AppConfig.h"

#include "globalVariables/GlobalVariables.h"

static const std::string kScene = "Settings";
static const std::string kGroup = "ApiConfig";

AppConfigData LoadAppConfig() {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    gv->LoadFile(kScene, kGroup);

    AppConfigData cfg;
    cfg.apiKey = *gv->AddValue<std::string>(kScene, kGroup, "ApiKey", std::string(""));
    cfg.llmSystemPrompt = *gv->AddValue<std::string>(kScene, kGroup, "LLM_SystemPrompt", cfg.llmSystemPrompt);
    cfg.visionPrompt = *gv->AddValue<std::string>(kScene, kGroup, "Vision_Prompt", cfg.visionPrompt);
    return cfg;
}

void SaveAppConfig(const AppConfigData& config) {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    gv->SetValue(kScene, kGroup, "ApiKey", config.apiKey);
    gv->SetValue(kScene, kGroup, "LLM_SystemPrompt", config.llmSystemPrompt);
    gv->SetValue(kScene, kGroup, "Vision_Prompt", config.visionPrompt);
    gv->SaveFile(kScene, kGroup);
}
