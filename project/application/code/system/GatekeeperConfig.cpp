#include "GatekeeperConfig.h"

#include "globalVariables/GlobalVariables.h"

static const std::string kScene = "Settings";
static const std::string kGroup = "Gatekeeper";

GatekeeperConfigData LoadGatekeeperConfig() {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    gv->LoadFile(kScene, kGroup);

    GatekeeperConfigData c; // 既定値（従来パネル初期値）を持つ
    c.camEnabled    = *gv->AddValue<bool>(kScene, kGroup, "CamEnabled", c.camEnabled);
    c.screenEnabled = *gv->AddValue<bool>(kScene, kGroup, "ScreenEnabled", c.screenEnabled);
    c.micEnabled    = *gv->AddValue<bool>(kScene, kGroup, "MicEnabled", c.micEnabled);
    c.camInterval    = *gv->AddValue<float>(kScene, kGroup, "CamInterval", c.camInterval);
    c.screenInterval = *gv->AddValue<float>(kScene, kGroup, "ScreenInterval", c.screenInterval);
    c.micInterval    = *gv->AddValue<float>(kScene, kGroup, "MicInterval", c.micInterval);
    c.combineWindow  = *gv->AddValue<float>(kScene, kGroup, "CombineWindow", c.combineWindow);
    c.autoEscalate     = *gv->AddValue<bool>(kScene, kGroup, "AutoEscalate", c.autoEscalate);
    c.escalateCooldown = *gv->AddValue<float>(kScene, kGroup, "EscalateCooldown", c.escalateCooldown);
    c.autoSpeak     = *gv->AddValue<bool>(kScene, kGroup, "AutoSpeak", c.autoSpeak);
    c.useLocalLLM   = *gv->AddValue<bool>(kScene, kGroup, "UseLocalLLM", c.useLocalLLM);
    c.useWebContext = *gv->AddValue<bool>(kScene, kGroup, "UseWebContext", c.useWebContext);
    c.autoObserveEnabled  = *gv->AddValue<bool>(kScene, kGroup, "AutoObserveEnabled", c.autoObserveEnabled);
    c.autoObserveWebCam   = *gv->AddValue<bool>(kScene, kGroup, "AutoObserveWebCam", c.autoObserveWebCam);
    c.autoObserveScreen   = *gv->AddValue<bool>(kScene, kGroup, "AutoObserveScreen", c.autoObserveScreen);
    c.autoObserveInterval = *gv->AddValue<float>(kScene, kGroup, "AutoObserveInterval", c.autoObserveInterval);

    c.ferModelPath = *gv->AddValue<std::string>(kScene, kGroup, "FerModelPath", c.ferModelPath);
    c.haarPath     = *gv->AddValue<std::string>(kScene, kGroup, "HaarPath", c.haarPath);
    c.camDeviceIndex = *gv->AddValue<int>(kScene, kGroup, "CamDeviceIndex", c.camDeviceIndex);
    c.camUseGpu     = *gv->AddValue<bool>(kScene, kGroup, "CamUseGpu", c.camUseGpu);
    c.camThreshold  = *gv->AddValue<float>(kScene, kGroup, "CamThreshold", c.camThreshold);
    c.scaleFactor   = *gv->AddValue<float>(kScene, kGroup, "ScaleFactor", c.scaleFactor);
    c.minNeighbors  = *gv->AddValue<int>(kScene, kGroup, "MinNeighbors", c.minNeighbors);
    c.minFaceSize   = *gv->AddValue<int>(kScene, kGroup, "MinFaceSize", c.minFaceSize);
    c.confirmFrames = *gv->AddValue<int>(kScene, kGroup, "ConfirmFrames", c.confirmFrames);
    c.ignoreNeutral = *gv->AddValue<bool>(kScene, kGroup, "IgnoreNeutral", c.ignoreNeutral);
    c.neutralBias   = *gv->AddValue<float>(kScene, kGroup, "NeutralBias", c.neutralBias);

    c.watchFg   = *gv->AddValue<bool>(kScene, kGroup, "WatchForeground", c.watchFg);
    c.detectNew = *gv->AddValue<bool>(kScene, kGroup, "DetectNewProcesses", c.detectNew);
    c.useScreenDiff = *gv->AddValue<bool>(kScene, kGroup, "UseScreenDiff", c.useScreenDiff);
    c.screenDiffThreshold = *gv->AddValue<float>(kScene, kGroup, "ScreenDiffThreshold", c.screenDiffThreshold);
    c.pixelDiffThreshold  = *gv->AddValue<int>(kScene, kGroup, "PixelDiffThreshold", c.pixelDiffThreshold);

    c.keywordsText  = *gv->AddValue<std::string>(kScene, kGroup, "Keywords", c.keywordsText);
    c.caseSensitive = *gv->AddValue<bool>(kScene, kGroup, "CaseSensitive", c.caseSensitive);
    return c;
}

void SaveGatekeeperConfig(const GatekeeperConfigData& c) {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    gv->SetValue(kScene, kGroup, "CamEnabled", c.camEnabled);
    gv->SetValue(kScene, kGroup, "ScreenEnabled", c.screenEnabled);
    gv->SetValue(kScene, kGroup, "MicEnabled", c.micEnabled);
    gv->SetValue(kScene, kGroup, "CamInterval", c.camInterval);
    gv->SetValue(kScene, kGroup, "ScreenInterval", c.screenInterval);
    gv->SetValue(kScene, kGroup, "MicInterval", c.micInterval);
    gv->SetValue(kScene, kGroup, "CombineWindow", c.combineWindow);
    gv->SetValue(kScene, kGroup, "AutoEscalate", c.autoEscalate);
    gv->SetValue(kScene, kGroup, "EscalateCooldown", c.escalateCooldown);
    gv->SetValue(kScene, kGroup, "AutoSpeak", c.autoSpeak);
    gv->SetValue(kScene, kGroup, "UseLocalLLM", c.useLocalLLM);
    gv->SetValue(kScene, kGroup, "UseWebContext", c.useWebContext);
    gv->SetValue(kScene, kGroup, "AutoObserveEnabled", c.autoObserveEnabled);
    gv->SetValue(kScene, kGroup, "AutoObserveWebCam", c.autoObserveWebCam);
    gv->SetValue(kScene, kGroup, "AutoObserveScreen", c.autoObserveScreen);
    gv->SetValue(kScene, kGroup, "AutoObserveInterval", c.autoObserveInterval);

    gv->SetValue(kScene, kGroup, "FerModelPath", c.ferModelPath);
    gv->SetValue(kScene, kGroup, "HaarPath", c.haarPath);
    gv->SetValue(kScene, kGroup, "CamDeviceIndex", c.camDeviceIndex);
    gv->SetValue(kScene, kGroup, "CamUseGpu", c.camUseGpu);
    gv->SetValue(kScene, kGroup, "CamThreshold", c.camThreshold);
    gv->SetValue(kScene, kGroup, "ScaleFactor", c.scaleFactor);
    gv->SetValue(kScene, kGroup, "MinNeighbors", c.minNeighbors);
    gv->SetValue(kScene, kGroup, "MinFaceSize", c.minFaceSize);
    gv->SetValue(kScene, kGroup, "ConfirmFrames", c.confirmFrames);
    gv->SetValue(kScene, kGroup, "IgnoreNeutral", c.ignoreNeutral);
    gv->SetValue(kScene, kGroup, "NeutralBias", c.neutralBias);

    gv->SetValue(kScene, kGroup, "WatchForeground", c.watchFg);
    gv->SetValue(kScene, kGroup, "DetectNewProcesses", c.detectNew);
    gv->SetValue(kScene, kGroup, "UseScreenDiff", c.useScreenDiff);
    gv->SetValue(kScene, kGroup, "ScreenDiffThreshold", c.screenDiffThreshold);
    gv->SetValue(kScene, kGroup, "PixelDiffThreshold", c.pixelDiffThreshold);

    gv->SetValue(kScene, kGroup, "Keywords", c.keywordsText);
    gv->SetValue(kScene, kGroup, "CaseSensitive", c.caseSensitive);
    gv->SaveFile(kScene, kGroup);
}

std::vector<std::string> SplitGatekeeperKeywords(const std::string& text) {
    std::vector<std::string> kws;
    std::string line;
    for (char ch : text) {
        if (ch == '\n' || ch == '\r') {
            if (!line.empty()) kws.push_back(line);
            line.clear();
        } else {
            line.push_back(ch);
        }
    }
    if (!line.empty()) kws.push_back(line);
    return kws;
}
