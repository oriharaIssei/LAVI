#pragma once

#include <string>
#include <vector>

namespace OriGine {
class Engine;
class GlobalVariables;
}

class FrameWork {
public:
    FrameWork();
    virtual ~FrameWork();

    virtual void Initialize(const std::vector<std::string>& _commandLines) = 0;
    virtual void Finalize()                                                = 0;
    virtual void Run()                                                     = 0;

protected:
    void ApplyWindowSettings();

    bool isEndRequest_                   = false;
    OriGine::Engine* engine_             = nullptr;
    OriGine::GlobalVariables* variables_ = nullptr;
};

void RegisterUsingComponents();
void RegisterUsingSystems();
