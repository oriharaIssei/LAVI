#include "OriGineDevGame.h"

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "input/InputManager.h"
#include "scene/SceneManager.h"

using namespace OriGine;

OriGineDevGame::OriGineDevGame()  = default;
OriGineDevGame::~OriGineDevGame() = default;

void OriGineDevGame::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    (void)_commandLines;

    RegisterUsingComponents();
    RegisterUsingSystems();
}

void OriGineDevGame::Finalize() {
    sceneManager_.reset();
    engine_->Finalize();
}

void OriGineDevGame::Run() {
    // TODO: メインループを実装
    while (!isEndRequest_) {
        engine_->BeginFrame();
        // sceneManager_->Update();
        // sceneManager_->Draw();
        engine_->EndFrame();
    }
}
