#include "LaviGame.h"

#define ENGINE_INCLUDE
#define RESOURCE_DIRECTORY
#include <EngineInclude.h>
#include "input/InputManager.h"

#include "directX12/RenderTexture.h"

#include "scene/SceneManager.h"
#include "scene/SceneJsonRegistry.h"

#include "globalVariables/GlobalVariables.h"

#include "system/LaviContext.h"
#include "system/SharedMediaContext.h"
#include "system/AppConfig.h" // LoadAppConfig
#include "system/WebAction.h" // LoadTagAxes

using namespace OriGine;

LaviGame::LaviGame()  = default;
LaviGame::~LaviGame() = default;

void LaviGame::Initialize(const std::vector<std::string>& _commandLines){
	variables_    = GlobalVariables::GetInstance();
	engine_       = Engine::GetInstance();
	sceneManager_ = std::make_unique<SceneManager>();

	variables_->LoadAllFile();
	engine_->Initialize();

	(void)_commandLines;

	InputManager* inputManager = InputManager::GetInstance();

	SceneJsonRegistry::GetInstance()->LoadAllScene(kApplicationResourceDirectory + "/" + kSceneJsonFolder);
	SceneJsonRegistry::GetInstance()->LoadAllEntityTemplates(kApplicationResourceDirectory + "/" + kEntityTemplateFolder);

	// シーン構築(BuildSceneByName)はコンポーネント/システムのレジストリを参照するため、
	// 必ず sceneManager_->Initialize() より前に登録しておく。
	// (未登録だと "Don't Registered Component" でコンポーネントがスキップされ空シーンになる)
	RegisterUsingComponents();
	RegisterUsingSystems();

	// アプリ設定・タグ軸は全 System の Initialize より前に決定的にロードする
	// （System 初期化順は unordered_map 由来で非決定。MemoryPanel 等が init 時に config.apiKey を読む）。
	LaviContext::Get().config = LoadAppConfig();
	LoadTagAxes("application/resource/memory/tag_axes.json");

	sceneManager_->Initialize(inputManager->GetKeyboard(),inputManager->GetMouse(),inputManager->GetGamePad());

	ApplyWindowSettings();
}

void LaviGame::Finalize(){
	sceneManager_.reset();
	engine_->Finalize();
}

void LaviGame::Run(){
	while(!isEndRequest_){
		if(engine_->ProcessMessage()){
			isEndRequest_ = true;
			break;
		}

		engine_->BeginFrame();

		// シーンの更新
		sceneManager_->Update();
		// シーンの描画 (シーンが変更されたら描画しない)
		sceneManager_->Render();

		// windowに描画
		OriGine::Engine::GetInstance()->ScreenPreDraw();
		sceneManager_->GetCurrentScene()->GetSceneView()->DrawTexture();
		OriGine::Engine::GetInstance()->ScreenPostDraw();

		engine_->EndFrame();

		// シーン変更の実行
		if(sceneManager_->IsChangeScene()){
			sceneManager_->ExecuteSceneChange();
		}
	}
}
