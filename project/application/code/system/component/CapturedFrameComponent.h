#pragma once

#include "component/IComponent.h" // engine ECS (engine/code/ECS は include パス上)

#include "CapturedFrame.h" // CapturedFrame（依存の無いスナップショット POD）

#include <nlohmann/json.hpp>
#include <memory>
#include <string>

/// <summary>
/// キャプチャフレームの ECS データレコード。ユニークエンティティに付与し、毎フレーム最新の
/// CapturedFrame スナップショットを保持する。旧来の共有スクラッチバッファ（ctx.camFrameBuffer 等）に
/// 各消費者が個別 GetLatestFrame していた方式を置き換える（1 デバイス 1 コピー・整合スナップショット）。
/// 直列化は source のみ（pixels/frame はランタイムのみ）。
/// </summary>
class CapturedFrameComponent : public OriGine::IComponent {
    friend void to_json(nlohmann::json& j, const CapturedFrameComponent& c);
    friend void from_json(const nlohmann::json& j, CapturedFrameComponent& c);

public:
    CapturedFrameComponent()           = default;
    ~CapturedFrameComponent() override = default;

    void Initialize(OriGine::Scene* _scene, OriGine::EntityHandle _owner) override;
    void Finalize() override;
    void Edit(OriGine::Scene* _scene, OriGine::EntityHandle _owner, const std::string& _parentLabel) override;

    int source = 0;                              // 0=Camera, 1=Screen
    std::shared_ptr<const CapturedFrame> frame;  // 最新スナップショット（ランタイムのみ）
};
