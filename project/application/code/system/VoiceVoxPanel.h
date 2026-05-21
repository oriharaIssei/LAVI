#pragma once

#include <future>
#include <memory>
#include <string>

#include "VoiceVoxClient.h"

struct SharedMediaContext;

class VoiceVoxPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    SharedMediaContext* ctx_ = nullptr;

    std::unique_ptr<VoiceVoxClient> voiceVox_;
    std::string voiceVoxEnginePath_ = "application/resource/voiceVox/windows-nvidia/run.exe";
    std::string voiceVoxText_;
};
