#include "SettingsUISystem.h"

#include "system/ui/UIRegistry.h"
#include "system/component/ui/UIElementComponent.h"
#include "system/component/ui/UITabBarComponent.h"

#include "scene/Scene.h"

#include <string>

namespace {
constexpr const char* kSettingsRootName = "Settings_Root";
} // namespace

SettingsUISystem::SettingsUISystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition, -50) {} // UILayoutSystem より先に走らせる

SettingsUISystem::~SettingsUISystem() = default;

void SettingsUISystem::Initialize() {
    // 「設定」ボタン → オーバーレイ開閉
    UIActionRegistry::Get().Register("settings.toggle", [this]() {
        open_ = !open_;
    });

    // サイドバー各カテゴリ → ページ切替
    for (int i = 0; i < kPageCount; ++i) {
        UIActionRegistry::Get().Register("settings.page." + std::to_string(i), [this, i]() {
            pendingPage_ = i;
            open_        = true; // カテゴリ選択時は開いた状態にしておく
        });
    }
}

void SettingsUISystem::Update() {
    OriGine::Scene* scene = GetScene();
    if (!scene) {
        return;
    }

    const OriGine::EntityHandle root = scene->GetUniqueEntity(kSettingsRootName);
    if (!scene->GetEntity(root)) {
        return; // まだシーンに居ない/未登録
    }

    // 開閉をルートの可視へ反映（子のサイドバー/ページは effectiveVisible 伝播で連動）。
    if (auto* elem = GetComponent<UIElementComponent>(root)) {
        elem->visible = open_;
    }

    // サイドバーで要求されたページをタブバーの activeIndex へ適用。
    if (pendingPage_ >= 0) {
        if (auto* tab = GetComponent<UITabBarComponent>(root)) {
            tab->activeIndex = pendingPage_;
        }
        pendingPage_ = -1;
    }
}

void SettingsUISystem::Finalize() {
    UIActionRegistry::Get().Unregister("settings.toggle");
    for (int i = 0; i < kPageCount; ++i) {
        UIActionRegistry::Get().Unregister("settings.page." + std::to_string(i));
    }
}
