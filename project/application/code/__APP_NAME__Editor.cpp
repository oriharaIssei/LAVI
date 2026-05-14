#include "__APP_NAME__Editor.h"

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"

#ifdef _DEBUG
#include "editor/EditorController.h"
#include "logger/Logger.h"
#endif

using namespace OriGine;

__APP_NAME__Editor::__APP_NAME__Editor()  = default;
__APP_NAME__Editor::~__APP_NAME__Editor() = default;

void __APP_NAME__Editor::Initialize(const std::vector<std::string>& _commandLines) {
    variables_    = GlobalVariables::GetInstance();
    engine_       = Engine::GetInstance();
    sceneManager_ = std::make_unique<SceneManager>();

    variables_->LoadAllFile();
    engine_->Initialize();

    (void)_commandLines;

    RegisterUsingComponents();
    RegisterUsingSystems();
}

void __APP_NAME__Editor::Finalize() {
    sceneManager_.reset();
    engine_->Finalize();
}

void __APP_NAME__Editor::Run() {
    // TODO: エディタループを実装
    while (!isEndRequest_) {
        engine_->BeginFrame();
        // EditorController::GetInstance()->Update();
        engine_->EndFrame();
    }
}
