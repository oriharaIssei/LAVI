#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/// <summary>
/// ドロップダウン（コンボボックス）ウィジェット。UIElementComponent と同居する。
/// ヘッダをクリックすると展開し、下方に選択肢を一覧表示する（UIInputSystem が開閉・項目クリックを処理、
/// UIPresentationSystem がヘッダ＋一覧を同居 SpriteRenderer/TextComponent へ反映）。
/// 選択肢(items)と確定の反映は role を見たドライバ System が行う（例: role="microphone" → MicSelectSystem）。
/// </summary>
class UIComboComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UIComboComponent& c);
    friend void from_json(const nlohmann::json& j, UIComboComponent& c);

public:
    UIComboComponent()           = default;
    ~UIComboComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    // --- 直列化対象 ---
    std::string role;        // データ供給/確定を担うドライバ System の識別子（例: "microphone"）
    std::string placeholder; // 未選択時に表示する文字列

    // --- 実行時状態（非直列化）---
    std::vector<std::string> items; // 選択肢（ドライバ System が毎フレーム供給）
    int selectedIndex  = -1;        // 現在の選択（items のインデックス）
    bool expanded      = false;     // 一覧を展開中か
    int requestedIndex = -1;        // ユーザーがクリックで選んだ項目（ドライバが消費して -1 に戻す）
};
