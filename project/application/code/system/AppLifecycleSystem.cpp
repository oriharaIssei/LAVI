#include "AppLifecycleSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "AppConfig.h" // AppConfigData（ctx.config.minimizeToTrayOnClose）

#include "winApp/WinApp.h"
#include "Engine.h"

AppLifecycleSystem::AppLifecycleSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

AppLifecycleSystem::~AppLifecycleSystem() = default;

void AppLifecycleSystem::Initialize() {
    SharedMediaContext& ctx = LaviContext::Get();

    // 注: config / タグ軸のロードは System 初期化（unordered_map 由来で順序非決定）より前に
    // 必要なため LaviGame/LaviEditor::Initialize（sceneManager 初期化の直前）で行う。
    // ここでは config が既にロード済みである前提で tray 設定を読む。

    // グローバルホットキー / システムトレイ。
    auto* winApp = OriGine::Engine::GetInstance()->GetWinApp();
    if (ctx.hotkeyEnabled) {
        hotkeyRegistered_ = winApp->RegisterGlobalHotkey(1, ctx.hotkeyModifiers, ctx.hotkeyVk);
    }
    winApp->EnableSystemTray(L"LAVI");
    winApp->SetMinimizeToTrayOnClose(ctx.config.minimizeToTrayOnClose);
}

void AppLifecycleSystem::Finalize() {
    auto* winApp = OriGine::Engine::GetInstance()->GetWinApp();
    if (hotkeyRegistered_) {
        winApp->UnregisterGlobalHotkey(1);
        hotkeyRegistered_ = false;
    }
    winApp->DisableSystemTray();
}
