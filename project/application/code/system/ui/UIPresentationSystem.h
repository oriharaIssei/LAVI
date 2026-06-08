#pragma once

#include "system/ISystem.h"

#include "ECS/entity/EntityHandle.h"

#include <cstdint>
#include <unordered_map>

/// <summary>
/// UI ウィジェットの状態を見た目へ反映する ECS システム（Category=Render、低 Priority で先行実行）。
/// 実描画は行わず、同居する TextComponent（ラベル/色/可視）と、存在すれば SpriteRenderer（背景の
/// 位置/サイズ/色/表示）へウィジェット状態を書き込む。実際の描画は既存の TextRenderSystem /
/// SpriteRenderSystem が担う。
/// - Button: 状態（通常/ホバー/押下）で色を切替、ラベルを反映。
/// - Slider: "label: value" を表示。
/// - TabBar: アクティブタブ名を表示。
///
/// MediaCaptureDemoSystem の ImGui 即時 UI を ECS リテインドモード UI へ置き換えるフレームワークの一部。
/// </summary>
class UIPresentationSystem : public OriGine::ISystem {
public:
    UIPresentationSystem();
    ~UIPresentationSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    // 展開中コンボの背景/文字を最前面へ持ち上げる前に保持しておく、各エンティティ本来の描画優先度
    // （Sprite, Text）。{owner -> {baseSpritePrio, baseTextPrio}}。
    std::unordered_map<OriGine::EntityHandle, std::pair<int32_t, int32_t>> basePrio_;
};
