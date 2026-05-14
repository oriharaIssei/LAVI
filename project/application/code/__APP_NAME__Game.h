#pragma once

#include "FrameWork.h"

#include <memory>

namespace OriGine {
class SceneManager;
}

class __APP_NAME__Game : public FrameWork {
public:
    __APP_NAME__Game();
    ~__APP_NAME__Game() override;

    void Initialize(const std::vector<std::string>& _commandLines) override;
    void Finalize() override;
    void Run() override;

private:
    std::unique_ptr<OriGine::SceneManager> sceneManager_ = nullptr;
};
