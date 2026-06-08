#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <string>

/// <summary>
/// 1 行テキスト入力ウィジェット。UIElementComponent と同居する。
/// クリックでフォーカスし、フォーカス中は WinApp::TakeTypedChars() の入力文字を取り込む
/// （UIInputSystem が駆動。Backspace=削除 / Enter=確定）。確定すると actionId をキーに
/// UIActionRegistry のテキストアクションへ現在テキストを渡して発火する（例: "chat.send"）。
/// 表示（入力中テキスト or プレースホルダ・キャレット・フォーカス枠）は UIPresentationSystem が
/// 同居 SpriteRenderer / TextComponent へ反映する。
/// </summary>
class UITextInputComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UITextInputComponent& c);
    friend void from_json(const nlohmann::json& j, UITextInputComponent& c);

public:
    UITextInputComponent()           = default;
    ~UITextInputComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    // --- 直列化対象 ---
    std::string actionId;                  // 確定時に発火する UIActionRegistry のテキストアクション
    std::string placeholder;               // 空のとき表示する淡色プレースホルダ
    bool submitClears   = true;            // 確定後にテキストをクリアするか
    int maxLength       = 512;             // 最大文字数（UTF-8 バイト数の上限）
    bool password       = false;           // true のとき表示を伏字（*）にする（API キー等）

    // --- 実行時状態（非直列化）---
    std::string text;                      // 現在の入力テキスト（UTF-8）
    bool focused = false;                  // 入力フォーカス中か
};
