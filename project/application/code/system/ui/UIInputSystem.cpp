#include "UIInputSystem.h"

#include "system/component/ui/UIButtonComponent.h"
#include "system/component/ui/UIElementComponent.h"
#include "system/component/ui/UISliderComponent.h"
#include "system/component/ui/UITabBarComponent.h"
#include "system/component/ui/UITextInputComponent.h"
#include "system/component/ui/UIChatLogComponent.h"
#include "system/component/ui/UIComboComponent.h"
#include "system/component/ui/UICheckboxComponent.h"
#include "system/ui/UIRegistry.h"

#include "scene/Scene.h"
#include "input/MouseInput.h"
#include "component/ComponentArray.h"
#include "component/text/TextComponent.h"
#include "component/transform/Transform2d.h" // ドラッグ移動で translate を編集

#include "Engine.h"
#include "winApp/WinApp.h"
#include "util/StringUtil.h"

#ifdef _DEBUG
#include "imgui/imgui.h" // エディタ専用: UI要素のドラッグ移動トグル
#endif

using OriGine::EntityHandle;
using OriGine::MouseButton;
using OriGine::Vec2f;

namespace {
// UTF-8 文字列末尾の 1 文字（先頭バイト + 後続バイト）を削除する。
void PopLastUtf8Char(std::string& s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    // 後続バイト (10xxxxxx) を遡る
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        --i;
    }
    s.erase(i);
}
} // namespace

UIInputSystem::UIInputSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {}

UIInputSystem::~UIInputSystem() = default;

void UIInputSystem::Initialize() {
    // TODO(phase2): 以下のデモ登録は実 System（Turn/Gatekeeper 等）側の Action/Binding 登録へ置換する。
    // フェーズ1の通し検証用: ボタン -> アクション -> バインド -> スライダー表示 の閉ループを作る。
    static float sDemoValue = 0.5f;
    UIBindingRegistry::Get().BindFloatPtr("ui.demo.value", &sDemoValue);
    UIActionRegistry::Get().Register("ui.demo.ping", []() {
        float v = UIBindingRegistry::Get().GetValue("ui.demo.value", 0.0f) + 0.1f;
        if (v > 1.0f) v = 0.0f;
        UIBindingRegistry::Get().SetValue("ui.demo.value", v);
    });
    UIActionRegistry::Get().Register("ui.demo.ping2", []() {
        float v = UIBindingRegistry::Get().GetValue("ui.demo.value", 0.0f) - 0.1f;
        if (v < 0.0f) v = 1.0f;
        UIBindingRegistry::Get().SetValue("ui.demo.value", v);
    });
}

void UIInputSystem::Finalize() {}

void UIInputSystem::Update() {
    OriGine::Scene* scene = GetScene();
    if (!scene) {
        return;
    }
    OriGine::MouseInput* mouse = scene->GetMouseInput();
    if (!mouse) {
        return;
    }

    const Vec2f mp  = mouse->GetPosition();
    const bool down = mouse->IsPress(MouseButton::LEFT);
    const bool trig = mouse->IsTrigger(MouseButton::LEFT);
    const bool rel  = mouse->IsRelease(MouseButton::LEFT);

    // このフレームに届いた入力文字（UTF-16）。フォーカス中のテキスト入力欄が消費する。
    std::wstring typedChars;
    if (auto* wa = OriGine::Engine::GetInstance()->GetWinApp()) {
        typedChars = wa->TakeTypedChars();
    }

#ifdef _DEBUG
    // --- エディタ専用: UI レイアウト編集トグル ---
    // UI 要素は Transform を持たず Gizmo で動かせず、UIPresentationSystem が毎フレーム
    // worldPos でスプライト位置を上書きするため SceneEditor 上では位置が固定に見える。
    // ON 中はシーンビューで要素をドラッグし、位置の源泉である Transform2d.translate を編集する。
    // Transform2d はシリアライズ対象なので SceneEditor の保存で永続化される。
    static bool sLayoutEditMode = false;
    if (ImGui::Begin("UI Layout Edit")) {
        ImGui::Checkbox("ドラッグで位置編集 (Transform2d)", &sLayoutEditMode);
        ImGui::TextUnformatted("ON中: シーンビューでUI要素をドラッグして移動。SceneEditorで保存して永続化。");
    }
    ImGui::End();
#endif

    auto* elemArr = GetComponentArray<UIElementComponent>();
    if (!elemArr) {
        return;
    }

#ifdef _DEBUG
    if (sLayoutEditMode) {
        // シーンビュー像に合わせ、SceneViewArea が毎フレーム設定する仮想マウス座標を使う
        // （GetPosition() は生ウィンドウ座標で像とズレるため不可）。
        const Vec2f mpv = mouse->GetVirtualPosition();
        static EntityHandle sDragOwner{};
        static bool sDragging = false;
        static Vec2f sLastMouse{};
        if (trig) {
            sDragging = false;
            for (auto& slot : elemArr->GetSlotsRef()) {
                for (auto& e : slot.components) {
                    if (e.effectiveVisible && e.HitTest(mpv)) {
                        sDragOwner = slot.owner; // スロット順で最後＝最前面を掴む
                        sDragging  = true;
                    }
                }
            }
            sLastMouse = mpv;
        }
        if (down && sDragging) {
            if (auto* t = GetComponent<OriGine::Transform2d>(sDragOwner)) {
                // 親は無スケール前提なので、画面差分＝local 差分として translate に加算。
                t->translate[0] += mpv[0] - sLastMouse[0];
                t->translate[1] += mpv[1] - sLastMouse[1];
            }
            sLastMouse = mpv;
        }
        if (rel) {
            sDragging = false;
        }
        return; // 編集中は通常のUI操作（ボタン押下・タブ切替等）を抑止
    }
#endif

    for (auto& slot : elemArr->GetSlotsRef()) {
        const EntityHandle owner = slot.owner;
        for (auto& e : slot.components) {
            if (!e.interactive || !e.effectiveVisible) {
                e.hovered = false;
                e.pressed = false;
                continue;
            }
            const bool hit = e.HitTest(mp);
            e.hovered      = hit;

            // --- Button ---
            if (auto* btn = GetComponent<UIButtonComponent>(owner)) {
                btn->clickedThisFrame = false;
                if (trig && hit) {
                    e.pressed = true;
                }
                if (rel) {
                    if (e.pressed && hit) {
                        btn->clickedThisFrame = true;
                        UIActionRegistry::Get().Invoke(btn->actionId);
                    }
                    e.pressed = false;
                }
            }

            // --- Checkbox ---
            if (auto* cb = GetComponent<UICheckboxComponent>(owner)) {
                if (trig && hit) {
                    e.pressed = true;
                }
                if (rel) {
                    if (e.pressed && hit) {
                        cb->toggleRequested = true; // ドライバ（MediaToggleSystem）が消費して反転
                    }
                    e.pressed = false;
                }
            }

            // --- Slider ---
            if (auto* sld = GetComponent<UISliderComponent>(owner)) {
                if (trig && hit) {
                    sld->dragging = true;
                }
                if (!down) {
                    sld->dragging = false;
                }
                if (sld->dragging) {
                    const float w = (e.size[0] > 0.0f) ? e.size[0] : 1.0f;
                    float t       = (mp[0] - e.worldPos[0]) / w;
                    t             = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                    sld->value    = sld->minValue + t * (sld->maxValue - sld->minValue);
                    UIBindingRegistry::Get().SetValue(sld->bindKey, sld->value);
                } else if (!sld->bindKey.empty() && UIBindingRegistry::Get().Has(sld->bindKey)) {
                    // 非ドラッグ時は実値を読み戻す（外部からの変更をスライダーへ反映）
                    sld->value = UIBindingRegistry::Get().GetValue(sld->bindKey, sld->value);
                }
            }

            // --- TextInput ---
            if (auto* ti = GetComponent<UITextInputComponent>(owner)) {
                // クリックでフォーカス切替（クリックした欄を focus、他は unfocus）。
                if (trig) {
                    ti->focused = hit;
                }
                if (ti->focused && !typedChars.empty()) {
                    std::wstring run; // 連続する印字文字はまとめて UTF-8 変換
                    auto flushRun = [&]() {
                        if (!run.empty()) {
                            ti->text += ConvertString(run);
                            run.clear();
                        }
                    };
                    for (wchar_t ch : typedChars) {
                        if (ch == 0x08) {            // Backspace
                            flushRun();
                            PopLastUtf8Char(ti->text);
                        } else if (ch == 0x0D || ch == 0x0A) { // Enter → 確定
                            flushRun();
                            if (!ti->text.empty()) {
                                UIActionRegistry::Get().InvokeText(ti->actionId, ti->text);
                                if (ti->submitClears) ti->text.clear();
                            }
                        } else if (ch == 0x1B) {     // Esc → フォーカス解除
                            flushRun();
                            ti->focused = false;
                        } else if (ch >= 0x20) {     // 印字可能文字
                            run.push_back(ch);
                        }
                    }
                    flushRun();
                    // 最大長（UTF-8 バイト数）を超えたら末尾文字を削る。
                    if (ti->maxLength > 0) {
                        while (static_cast<int>(ti->text.size()) > ti->maxLength) {
                            PopLastUtf8Char(ti->text);
                        }
                    }
                }
            }

            // --- Combo（ドロップダウン）---
            if (auto* combo = GetComponent<UIComboComponent>(owner)) {
                float lineH = 22.0f;
                if (auto* t = GetComponent<OriGine::TextComponent>(owner)) {
                    lineH = t->fontSize * (t->lineSpacing > 0.0f ? t->lineSpacing : 1.0f);
                }
                const int rows  = combo->expanded ? (1 + static_cast<int>(combo->items.size())) : 1;
                const float totalH = lineH * static_cast<float>(rows);
                const bool inX  = mp[0] >= e.worldPos[0] && mp[0] <= e.worldPos[0] + e.size[0];
                const bool inY  = mp[1] >= e.worldPos[1] && mp[1] <= e.worldPos[1] + totalH;
                if (trig) {
                    if (inX && inY) {
                        const int row = static_cast<int>((mp[1] - e.worldPos[1]) / lineH);
                        if (row <= 0) {
                            combo->expanded = !combo->expanded; // ヘッダ → 開閉
                        } else if (combo->expanded && (row - 1) < static_cast<int>(combo->items.size())) {
                            combo->requestedIndex = row - 1; // 項目選択（ドライバが反映）
                            combo->expanded = false;
                        }
                    } else if (combo->expanded) {
                        combo->expanded = false; // 外側クリックで閉じる
                    }
                }
            }

            // --- ChatLog（ホイールでスクロール）---
            if (auto* log = GetComponent<UIChatLogComponent>(owner)) {
                if (hit) {
                    const int wheel = mouse->GetWheelDelta();
                    if (wheel > 0) {
                        log->scrollOffset += 3; // 上方向（古い履歴へ）
                    } else if (wheel < 0) {
                        log->scrollOffset -= 3; // 下方向（新しい履歴へ）
                        if (log->scrollOffset < 0) log->scrollOffset = 0;
                    }
                    // 上限は総行数−可視行数に依存するため UIPresentationSystem でクランプする。
                }
            }

            // --- TabBar ---
            if (auto* tb = GetComponent<UITabBarComponent>(owner)) {
                if (trig && hit && !tb->tabs.empty()) {
                    const int n   = static_cast<int>(tb->tabs.size());
                    const float seg = e.size[0] / static_cast<float>(n);
                    int idx       = (seg > 0.0f) ? static_cast<int>((mp[0] - e.worldPos[0]) / seg) : 0;
                    idx           = idx < 0 ? 0 : (idx >= n ? n - 1 : idx);
                    tb->activeIndex = idx;
                }
            }
        }
    }
}
