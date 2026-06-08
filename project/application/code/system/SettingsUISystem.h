#pragma once

#include "system/ISystem.h"

/// <summary>
/// 設定オーバーレイ（左サイドバー＋ページ）の表示制御を行う ECS システム（Category=StateTransition）。
///
/// - UIActionRegistry に "settings.toggle"（開閉）と "settings.page.0".."N"（カテゴリ切替）を登録する。
/// - 毎フレーム、一意エンティティ "Settings_Root" の UIElementComponent.visible を open_ に反映し、
///   サイドバーで選ばれたページ番号を同居 UITabBarComponent.activeIndex へ適用する。
///   ページ可視は既存 UILayoutSystem（tabBarUuid/index）が解決し、閉じると effectiveVisible 伝播で
///   サイドバー・ページごと隠れる。
///
/// 旧 ImGui パネルが _DEBUG 限定だったため、Release でマイク/画面共有/カメラ等を設定するための土台。
/// </summary>
class SettingsUISystem : public OriGine::ISystem {
public:
    SettingsUISystem();
    ~SettingsUISystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    static constexpr int kPageCount = 4; // 音声・ビデオ / グラフィック / サウンド / LLM

    bool open_       = false;
    int pendingPage_ = -1; // サイドバークリックで要求されたページ（適用後 -1）
};
