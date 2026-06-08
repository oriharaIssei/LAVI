#include "UILayoutSystem.h"

#include "system/component/ui/UIElementComponent.h"
#include "system/component/ui/UIPageComponent.h"
#include "system/component/ui/UITabBarComponent.h"

#include "Engine.h"
#include "winApp/WinApp.h"
#include "component/ComponentArray.h"
#include "component/transform/Transform2d.h" // UI の worldPos を親子付き2D Transform から解決する

#include <uuid/uuid.h>

#include <functional>
#include <string>
#include <unordered_map>

using OriGine::Vec2f;

UILayoutSystem::UILayoutSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

UILayoutSystem::~UILayoutSystem() = default;

void UILayoutSystem::Initialize() {}

void UILayoutSystem::Finalize() {}

void UILayoutSystem::Update() {
    auto* elemArr = GetComponentArray<UIElementComponent>();
    if (!elemArr) {
        return;
    }

    const Vec2f screen = OriGine::Engine::GetInstance()->GetWinApp()->GetWindowSize();

    // 1. uuid -> UIElementComponent* の索引を作る
    std::unordered_map<std::string, UIElementComponent*> elemByUuid;
    for (auto& slot : elemArr->GetSlotsRef()) {
        const std::string key = uuids::to_string(slot.owner.uuid);
        for (auto& e : slot.components) {
            elemByUuid[key] = &e;
        }
    }

    // 2. タブバーの activeIndex に応じてページ可視を決める（祖先 → 子の解決前に行う）
    std::unordered_map<std::string, int> activeByTabBar;
    if (auto* tabArr = GetComponentArray<UITabBarComponent>()) {
        for (auto& slot : tabArr->GetSlotsRef()) {
            const std::string key = uuids::to_string(slot.owner.uuid);
            for (auto& t : slot.components) {
                activeByTabBar[key] = t.activeIndex;
            }
        }
    }
    if (auto* pageArr = GetComponentArray<UIPageComponent>()) {
        for (auto& slot : pageArr->GetSlotsRef()) {
            const std::string key = uuids::to_string(slot.owner.uuid);
            for (auto& p : slot.components) {
                auto ait = activeByTabBar.find(p.tabBarUuid);
                auto eit = elemByUuid.find(key);
                if (ait != activeByTabBar.end() && eit != elemByUuid.end()) {
                    eit->second->visible = (ait->second == p.index);
                }
            }
        }
    }

    // 2.5 uuid -> Transform2d* の索引（位置の源泉）。各 UI 要素が所持する Transform2d を引く。
    std::unordered_map<std::string, OriGine::Transform2d*> t2dByUuid;
    if (auto* t2dArr = GetComponentArray<OriGine::Transform2d>()) {
        for (auto& slot : t2dArr->GetSlotsRef()) {
            const std::string key = uuids::to_string(slot.owner.uuid);
            for (auto& t : slot.components) {
                t2dByUuid[key] = &t;
            }
        }
    }

    // 3. 親子を辿って worldPos / effectiveVisible を解決（メモ化再帰）。
    //    位置は Transform2d があれば親子付き行列から、無ければ従来の anchor/offset から求める。
    std::unordered_map<std::string, bool> resolved;
    std::function<void(const std::string&)> resolve = [&](const std::string& key) {
        if (resolved[key]) {
            return;
        }
        resolved[key] = true; // 循環参照ガード（先にマーク）

        auto eit = elemByUuid.find(key);
        if (eit == elemByUuid.end()) {
            return;
        }
        UIElementComponent* e = eit->second;

        Vec2f parentPos                = {0.0f, 0.0f};
        Vec2f parentSize               = screen;
        bool parentVis                 = true;
        OriGine::Transform2d* parentT2d = nullptr;
        if (!e->parentUuid.empty()) {
            auto pit = elemByUuid.find(e->parentUuid);
            if (pit != elemByUuid.end()) {
                resolve(e->parentUuid); // 親を先に解決（親の Transform2d::UpdateMatrix を先に走らせる）
                parentPos  = pit->second->worldPos;
                parentSize = pit->second->size;
                parentVis  = pit->second->effectiveVisible;
            }
            auto ptit = t2dByUuid.find(e->parentUuid);
            if (ptit != t2dByUuid.end()) {
                parentT2d = ptit->second;
            }
        }

        auto t2dit = t2dByUuid.find(key);
        if (t2dit != t2dByUuid.end()) {
            OriGine::Transform2d* t = t2dit->second;
            t->parent               = parentT2d; // 親要素の Transform2d を親に（親子付）
            t->UpdateMatrix();                   // 親は解決済み → worldMat に反映
            e->worldPos = t->GetWorldTranslate();
        } else {
            // フォールバック: Transform2d を持たない要素は従来の anchor/offset で配置
            e->worldPos = {
                parentPos[0] + e->anchor[0] * parentSize[0] + e->offsetPos[0],
                parentPos[1] + e->anchor[1] * parentSize[1] + e->offsetPos[1]};
        }
        e->effectiveVisible = e->visible && parentVis;
    };

    for (auto& [uuid, e] : elemByUuid) {
        (void)e;
        resolve(uuid);
    }
}
