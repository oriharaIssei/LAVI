#pragma once

#include "system/ISystem.h"

/// <summary>
/// アプリ全体の初期化・終了を担う ECS システム（Category=Initialize）。
/// MediaCaptureDemoSystem(DEBUG 限定) の Initialize/Finalize から、UI に依存しない
/// アプリレベルの起動処理を切り出したもの（ECS 分解）。Release でも常時稼働させ、
/// 調整 UI 無しでアプリが成立するようにする。
///
/// 担当:
/// - グローバルホットキー登録 / システムトレイ初期化（WinApp API）。
///
/// 注: アプリ設定(config)・タグ軸のロードは System 初期化順が非決定（unordered_map）なため、
/// それより前が保証される LaviGame/LaviEditor::Initialize（sceneManager 初期化の直前）で行う。
/// 本 System はその後に走り、config が既にロード済みである前提で tray 設定を読む。
/// </summary>
class AppLifecycleSystem : public OriGine::ISystem {
public:
    AppLifecycleSystem();
    ~AppLifecycleSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // 常駐処理は無し（起動/終了のみ）

private:
    bool hotkeyRegistered_ = false;
};
