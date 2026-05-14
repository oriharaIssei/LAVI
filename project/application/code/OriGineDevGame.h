#pragma once

#include "FrameWork.h"

#include <memory>

namespace OriGine {
class SceneManager;
}

class OriGineDevGame : public FrameWork {
public:
    OriGineDevGame();
    ~OriGineDevGame() override;

    void Initialize(const std::vector<std::string>& _commandLines) override;
    void Finalize() override;
    void Run() override;

private:
    std::unique_ptr<OriGine::SceneManager> sceneManager_ = nullptr;
};
