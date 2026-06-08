#pragma once

#include <string>

struct SharedMediaContext;

// VoiceVox の調整 UI（DEBUG 限定）。VoiceVoxClient の所有・エンジン起動は VoiceVoxSystem へ移譲（ECS 分解）。
// Draw 入口で LaviContext::Get().voiceVox を pull して操作する（init 順非依存・null ガード）。
class VoiceVoxPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void Draw();

private:
    SharedMediaContext* ctx_ = nullptr;

    std::string voiceVoxText_;
};
