#include "LaviEditor.h"

#ifdef _DEBUG
#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>

#include "globalVariables/GlobalVariables.h"
#include "scene/SceneManager.h"

#include "editor/EditorController.h"
#include "editor/sceneEditor/SceneEditor.h"
#include "editor/setting/SettingWindow.h"
#include "logger/Logger.h"

#include "system/LaviContext.h"
#include "system/SharedMediaContext.h"
#include "system/AppConfig.h" // LoadAppConfig
#include "system/WebAction.h" // LoadTagAxes

using namespace OriGine;

LaviEditor::LaviEditor()  = default;
LaviEditor::~LaviEditor() = default;

void LaviEditor::Initialize(const std::vector<std::string>& _commandLines){
	variables_    = GlobalVariables::GetInstance();
	engine_       = Engine::GetInstance();
	sceneManager_ = std::make_unique<SceneManager>();

	variables_->LoadAllFile();
	engine_->Initialize();

	(void)_commandLines;

	RegisterUsingComponents();
	RegisterUsingSystems();

	// アプリ設定・タグ軸はシーン構築（= 各 System の Initialize）より前に決定的にロードする
	// （System 初期化順は unordered_map 由来で非決定。MemoryPanel 等が init 時に config.apiKey を読む）。
	LaviContext::Get().config = LoadAppConfig();
	LoadTagAxes("application/resource/memory/tag_axes.json");

	ApplyWindowSettings();

	EditorController::GetInstance()->AddEditor<SceneEditorWindow>(std::make_unique<SceneEditorWindow>());
	EditorController::GetInstance()->AddEditor<SettingWindow>(std::make_unique<SettingWindow>());
	EditorController::GetInstance()->Initialize();
}

void LaviEditor::Finalize(){
	EditorController::GetInstance()->Finalize();
	sceneManager_.reset();
	engine_->Finalize();
}

void LaviEditor::Run(){
	while(!isEndRequest_){
		if(engine_->ProcessMessage()){
			isEndRequest_ = true;
			break;
		}

		engine_->BeginFrame();
		EditorController::GetInstance()->Update();
		engine_->EndFrame();

		engine_->ScreenPreDraw();
		engine_->ScreenPostDraw();
	}
}

#endif // _DEBUG