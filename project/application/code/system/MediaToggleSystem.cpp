#include "MediaToggleSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "GatekeeperConfig.h"
#include "GatekeeperManager.h"
#include "AppConfig.h"
#include "system/component/ui/UICheckboxComponent.h"

#include "component/ComponentArray.h"
#include "winApp/WinApp.h"
#include "Engine.h"

#include <string>

namespace {
// role に対応する GatekeeperConfigData のフラグ参照を返す。未知 role は nullptr。
bool* CfgFlag(GatekeeperConfigData& cfg, const std::string& role) {
    if (role == "mic.enabled")        return &cfg.micEnabled;
    if (role == "screen.enabled")     return &cfg.screenEnabled;
    if (role == "camera.enabled")     return &cfg.camEnabled;
    if (role == "llm.useLocal")       return &cfg.useLocalLLM;
    if (role == "llm.useWebContext")  return &cfg.useWebContext;
    if (role == "llm.autoObserve")    return &cfg.autoObserveEnabled;
    return nullptr;
}
// role に対応する動作中 Config（GatekeeperManager）のフラグ参照を返す。未知 role は nullptr。
bool* LiveFlag(GatekeeperManager::Config& c, const std::string& role) {
    if (role == "mic.enabled")        return &c.micEnabled;
    if (role == "screen.enabled")     return &c.screenEnabled;
    if (role == "camera.enabled")     return &c.camEnabled;
    if (role == "llm.useLocal")       return &c.useLocalLLM;
    if (role == "llm.useWebContext")  return &c.useWebContext;
    if (role == "llm.autoObserve")    return &c.autoObserveEnabled;
    return nullptr;
}
} // namespace

MediaToggleSystem::MediaToggleSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

MediaToggleSystem::~MediaToggleSystem() = default;

void MediaToggleSystem::Update() {
    auto* arr = GetComponentArray<UICheckboxComponent>();
    if (!arr) {
        return;
    }
    GatekeeperManager* gk = LaviContext::Get().gkManager;

    for (auto& slot : arr->GetSlotsRef()) {
        for (auto& cb : slot.components) {
            // 閉じる(X)ボタンでトレイ最小化するか（AppConfig）。WinApp に即時反映する。
            if (cb.role == "tray.minimizeOnClose") {
                auto* winApp = OriGine::Engine::GetInstance()->GetWinApp();
                if (cb.toggleRequested) {
                    AppConfigData cfg = LoadAppConfig();
                    cfg.minimizeToTrayOnClose = !cfg.minimizeToTrayOnClose;
                    SaveAppConfig(cfg);
                    if (winApp) {
                        winApp->SetMinimizeToTrayOnClose(cfg.minimizeToTrayOnClose);
                    }
                    cb.checked         = cfg.minimizeToTrayOnClose;
                    cb.toggleRequested = false;
                } else {
                    cb.checked = winApp ? winApp->IsMinimizeToTrayOnClose()
                                        : LoadAppConfig().minimizeToTrayOnClose;
                }
                continue;
            }

            bool* plive = gk ? LiveFlag(gk->config(), cb.role) : nullptr;

            if (cb.toggleRequested) {
                // クリック確定 → 設定ファイルのフラグを反転して永続化し、動作中 Config にも即時反映。
                GatekeeperConfigData cfg = LoadGatekeeperConfig();
                bool* pcfg = CfgFlag(cfg, cb.role);
                if (!pcfg) {
                    cb.toggleRequested = false;
                    continue;
                }
                const bool next = !(*pcfg);
                *pcfg = next;
                SaveGatekeeperConfig(cfg);
                if (plive) {
                    *plive = next;
                }
                cb.checked         = next;
                cb.toggleRequested = false;
            } else if (plive) {
                // 動作中の値に追従（毎フレームのファイル読み込みは避ける）。
                cb.checked = *plive;
            } else {
                // GatekeeperManager 未初期化時のみ設定ファイル値を表示に使う。
                GatekeeperConfigData cfg = LoadGatekeeperConfig();
                if (bool* pcfg = CfgFlag(cfg, cb.role)) {
                    cb.checked = *pcfg;
                }
            }
        }
    }
}
