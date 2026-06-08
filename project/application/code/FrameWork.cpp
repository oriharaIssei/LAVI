#include "FrameWork.h"

/// engine include
#define ENGINE_INCLUDE
#define ENGINE_ECS
#define ENGINE_COMPONENTS
#define ENGINE_SYSTEMS
#include <EngineInclude.h>

/// engine
#include "Engine.h"
#include "directX12/DxSwapChain.h"
#include "globalVariables/GlobalVariables.h"
#include "component/transform/Transform2d.h" // UI 要素の位置・親子付に使う2D Transform

/// application components
#include "system/component/CapturePromptComponent.h"
#include "system/component/ui/UIElementComponent.h"
#include "system/component/ui/UIButtonComponent.h"
#include "system/component/ui/UISliderComponent.h"
#include "system/component/ui/UITabBarComponent.h"
#include "system/component/ui/UIPageComponent.h"
#include "system/component/ui/UITextInputComponent.h"
#include "system/component/ui/UIChatLogComponent.h"
#include "system/component/ui/UIComboComponent.h"
#include "system/component/ui/UICheckboxComponent.h"

/// application systems
#include "system/MediaCaptureDemoSystem.h"
#include "system/ui/UILayoutSystem.h"
#include "system/ui/UIInputSystem.h"
#include "system/ui/UIPresentationSystem.h"
#include "system/SettingsUISystem.h"
#include "system/AppLifecycleSystem.h"
#include "system/LocationSystem.h"
#include "system/WebSearchSystem.h"
#include "system/KnowledgeSystem.h"
#include "system/ActionSystem.h"
#include "system/VisionSystem.h"
#include "system/MicrophoneSystem.h"
#include "system/MemorySystem.h"
#include "system/VoiceVoxSystem.h"
#include "system/WebCameraSystem.h"
#include "system/ScreenCaptureSystem.h"
#include "system/TextChatSystem.h"
#include "system/MicSelectSystem.h"
#include "system/CameraSelectSystem.h"
#include "system/MediaToggleSystem.h"
#include "system/LLMSettingsSystem.h"
#include "system/VoiceSettingsSystem.h"
#include "system/PersonaGenerationSystem.h"
#include "system/AutoObserveSystem.h"
#include "system/WakeWordSystem.h"
#include "system/TurnSystem.h"
#include "system/CameraGateSystem.h"
#include "system/ScreenGateSystem.h"
#include "system/MicGateSystem.h"
#include "system/GatekeeperSystem.h"

using namespace OriGine;

FrameWork::FrameWork()  = default;
FrameWork::~FrameWork() = default;

void FrameWork::ApplyWindowSettings(){
#ifndef _DEBUG
	GlobalVariables* gv = GlobalVariables::GetInstance();
	auto* scene = gv->GetScene("Settings");
	if(!scene){
		return;
	}
	auto groupItr = scene->find("WindowState");
	if(groupItr == scene->end()){
		return;
	}

	float r = *gv->AddValue<float>("Settings","WindowState","ClearColorR",0.0f);
	float g = *gv->AddValue<float>("Settings","WindowState","ClearColorG",0.0f);
	float b = *gv->AddValue<float>("Settings","WindowState","ClearColorB",0.0f);
	float a = *gv->AddValue<float>("Settings","WindowState","ClearColorA",0.0f);
	Engine::GetInstance()->GetDxSwapChain()->SetClearColor(Vec4f(r,g,b,a));
#endif
}

void RegisterUsingComponents(){
	ComponentRegistry* componentRegistry = ComponentRegistry::GetInstance();

	componentRegistry->RegisterComponent<Transform>();
	componentRegistry->RegisterComponent<Transform2d>(); // UI 要素の位置・親子付（worldPos の源泉）
	componentRegistry->RegisterComponent<CameraTransform>();
	componentRegistry->RegisterComponent<TextComponent>();
	componentRegistry->RegisterComponent<TextStreamComponent>();
	componentRegistry->RegisterComponent<SpriteRenderer>(); // UI 背景クワッド（白1x1 を color で塗る）

	componentRegistry->RegisterComponent<CapturePromptComponent>();

	// UI フレームワーク（ECS リテインドモード UI）
	componentRegistry->RegisterComponent<UIElementComponent>();
	componentRegistry->RegisterComponent<UIButtonComponent>();
	componentRegistry->RegisterComponent<UISliderComponent>();
	componentRegistry->RegisterComponent<UITabBarComponent>();
	componentRegistry->RegisterComponent<UIPageComponent>();
	componentRegistry->RegisterComponent<UITextInputComponent>();
	componentRegistry->RegisterComponent<UIChatLogComponent>();
	componentRegistry->RegisterComponent<UIComboComponent>();
	componentRegistry->RegisterComponent<UICheckboxComponent>();
}

void RegisterUsingSystems(){
	SystemRegistry* systemRegistry = SystemRegistry::GetInstance();

	// AppLifecycleSystem は config ロードを担うため最初に登録（他 System の Initialize より先に走らせる）。
	systemRegistry->RegisterSystem<AppLifecycleSystem>();
	systemRegistry->RegisterSystem<CameraInitialize>();
	systemRegistry->RegisterSystem<LocationSystem>();
	systemRegistry->RegisterSystem<WebSearchSystem>();
	systemRegistry->RegisterSystem<KnowledgeSystem>();
	systemRegistry->RegisterSystem<ActionSystem>();
	systemRegistry->RegisterSystem<VisionSystem>(); // 画像解析（VisionAnalyzer 所有・公開。ECS 分解）
	// 機能ロジックの常時稼働 System（Release でも動く＝旧モノリスからの分解）。
	systemRegistry->RegisterSystem<MicrophoneSystem>();
	systemRegistry->RegisterSystem<MemorySystem>();
	systemRegistry->RegisterSystem<VoiceVoxSystem>();
	systemRegistry->RegisterSystem<WebCameraSystem>();
	systemRegistry->RegisterSystem<ScreenCaptureSystem>();
	systemRegistry->RegisterSystem<TextChatSystem>();
	systemRegistry->RegisterSystem<MicSelectSystem>();
	systemRegistry->RegisterSystem<CameraSelectSystem>();
	systemRegistry->RegisterSystem<MediaToggleSystem>(); // マイク/画面/カメラ使用チェックボックスのドライバ
	systemRegistry->RegisterSystem<LLMSettingsSystem>(); // LLM 設定（API キー/ローカル切替）のドライバ
	systemRegistry->RegisterSystem<VoiceSettingsSystem>(); // サウンド設定（VoiceVox 話者選択）のドライバ
	systemRegistry->RegisterSystem<PersonaGenerationSystem>(); // ペルソナ生成（説明→システムプロンプト）
	systemRegistry->RegisterSystem<AutoObserveSystem>(); // 自律観察（一定間隔でカメラ/画面にコメント）
	systemRegistry->RegisterSystem<WakeWordSystem>();
	systemRegistry->RegisterSystem<TurnSystem>();
	systemRegistry->RegisterSystem<CameraGateSystem>();
	systemRegistry->RegisterSystem<ScreenGateSystem>();
	systemRegistry->RegisterSystem<MicGateSystem>();
	systemRegistry->RegisterSystem<GatekeeperSystem>();

#ifdef DEBUG
	systemRegistry->RegisterSystem<MediaCaptureDemoSystem>();
#endif // DEBUG

	systemRegistry->RegisterSystem<SpriteRenderSystem>();
	systemRegistry->RegisterSystem<TextRenderSystem>();
	systemRegistry->RegisterSystem<TextBoundsRenderSystem>();
	systemRegistry->RegisterSystem<TextStreamSystem>();

	// UI フレームワーク（ECS リテインドモード UI）
	systemRegistry->RegisterSystem<SettingsUISystem>(); // 設定オーバーレイ制御（UILayoutSystem より先）
	systemRegistry->RegisterSystem<UILayoutSystem>();
	systemRegistry->RegisterSystem<UIInputSystem>();
	systemRegistry->RegisterSystem<UIPresentationSystem>();

}
