#pragma once

#include "component/IComponent.h" // engine ECS (engine/code/ECS は include パス上)

#include <nlohmann/json.hpp>
#include <string>

/// <summary>
/// キャプチャ系 System (Camera/Screen/Mic) が検知した 1 件の「プロンプト」を表す Component。
/// GatekeeperSystem がこれを走査し、会話を発生させるか破棄するかを決める（ECS データフロー）。
/// 旧 GatekeeperManager の GateEvent に対応する。実行時のみ生成される短命エンティティに付与する。
/// </summary>
class CapturePromptComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const CapturePromptComponent& c);
    friend void from_json(const nlohmann::json& j, CapturePromptComponent& c);

public:
    CapturePromptComponent()           = default;
    ~CapturePromptComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    int source = 0;          // GateSource: 0=Camera, 1=Screen, 2=Mic
    std::string description; // 人間可読の検知内容（LLM へのコンテキスト）
    double time = 0.0;       // 検知時刻（秒）
};
