#pragma once

#include "component/IComponent.h"

#include <nlohmann/json.hpp>
#include <string>

/// <summary>
/// 会話履歴表示ウィジェット。UIElementComponent と同居する。
/// LaviContext::Get().conversationMemory の直近エントリ（user/assistant）を整形して
/// 同居 TextComponent へ表示する（UIPresentationSystem が駆動）。読み取り専用・非インタラクティブ。
/// </summary>
class UIChatLogComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const UIChatLogComponent& c);
    friend void from_json(const nlohmann::json& j, UIChatLogComponent& c);

public:
    UIChatLogComponent()           = default;
    ~UIChatLogComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    // --- 直列化対象 ---
    int maxTurns               = 200;           // 履歴として保持・走査する直近エントリ数の上限
    std::string userPrefix     = "あなた: ";    // user 行の接頭辞
    std::string assistantPrefix = "LAVI: ";     // assistant 行の接頭辞

    // --- 実行時状態（非直列化）---
    int scrollOffset = 0;  // 最下部（最新）からの行スクロール量。0=最新を表示。UIInputSystem がホイールで増減、
                           // UIPresentationSystem が総行数−可視行数で上限クランプする。
};
