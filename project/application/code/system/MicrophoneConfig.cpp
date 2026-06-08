#include "MicrophoneConfig.h"

#include "globalVariables/GlobalVariables.h"

static const std::string kScene = "Settings";
static const std::string kGroup = "Microphone";

MicrophoneConfigData LoadMicrophoneConfig() {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    gv->LoadFile(kScene, kGroup);

    MicrophoneConfigData c;
    c.autoStartCapture = *gv->AddValue<bool>(kScene, kGroup, "AutoStartCapture", c.autoStartCapture);
    c.autoLoadModel    = *gv->AddValue<bool>(kScene, kGroup, "AutoLoadModel", c.autoLoadModel);
    c.micDeviceIndex   = *gv->AddValue<int>(kScene, kGroup, "MicDeviceIndex", c.micDeviceIndex);
    c.whisperModelPath = *gv->AddValue<std::string>(kScene, kGroup, "WhisperModelPath", c.whisperModelPath);
    c.vadModelPath     = *gv->AddValue<std::string>(kScene, kGroup, "VadModelPath", c.vadModelPath);
    return c;
}

void SaveMicrophoneConfig(const MicrophoneConfigData& c) {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    gv->SetValue(kScene, kGroup, "AutoStartCapture", c.autoStartCapture);
    gv->SetValue(kScene, kGroup, "AutoLoadModel", c.autoLoadModel);
    gv->SetValue(kScene, kGroup, "MicDeviceIndex", c.micDeviceIndex);
    gv->SetValue(kScene, kGroup, "WhisperModelPath", c.whisperModelPath);
    gv->SetValue(kScene, kGroup, "VadModelPath", c.vadModelPath);
    gv->SaveFile(kScene, kGroup);
}
