#include "__APP_NAME__Game.h"

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "input/InputManager.h"
#include "scene/SceneManager.h"

using namespace OriGine;

__APP_NAME__Game::__APP_NAME__Game()  = default;
__APP_NAME__Game::~__APP_NAME__Game() = default;

void __APP_NAME__Game::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    (void)_commandLines;

    RegisterUsingComponents();
    RegisterUsingSystems();
}

void __APP_NAME__Game::Finalize() {
    sceneManager_.reset();
    engine_->Finalize();
}

void __APP_NAME__Game::Run() {
    // TODO: メインループを実装
    while (!isEndRequest_) {
        engine_->BeginFrame();
        // sceneManager_->Update();
        // sceneManager_->Draw();
        engine_->EndFrame();
    }
}
