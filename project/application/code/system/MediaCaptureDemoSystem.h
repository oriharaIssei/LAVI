#pragma once

#include "system/ISystem.h"

#include <memory>

struct SharedMediaContext;
class MicrophonePanel;
class WebCameraPanel;
class ScreenCapturePanel;
class VoiceVoxPanel;
class VisionPanel;
class LLMChatPanel;
class GatekeeperManager;
class GatekeeperPanel;
class MemoryPanel;

class MediaCaptureDemoSystem
    : public OriGine::ISystem
{
public:
    MediaCaptureDemoSystem();
    ~MediaCaptureDemoSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<SharedMediaContext> ctx_;
    std::unique_ptr<MicrophonePanel> micPanel_;
    std::unique_ptr<WebCameraPanel> camPanel_;
    std::unique_ptr<ScreenCapturePanel> screenPanel_;
    std::unique_ptr<VoiceVoxPanel> voiceVoxPanel_;
    std::unique_ptr<VisionPanel> visionPanel_;
    std::unique_ptr<LLMChatPanel> llmPanel_;
    std::unique_ptr<GatekeeperManager> gkManager_;
    std::unique_ptr<GatekeeperPanel> gkPanel_;
    std::unique_ptr<MemoryPanel> memoryPanel_;
};
