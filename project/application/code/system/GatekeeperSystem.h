#pragma once

#include "system/ISystem.h"

#include <memory>

class GatekeeperManager;

/// <summary>
/// ゲートキーパー統合層（GatekeeperManager）を駆動する ECS システム（Category=Movement）。
/// 3つのキャプチャ評価・意図分類・LLM 送出・発話を毎フレーム回す。
/// 消費側（TurnSystem / GatekeeperPanel / MemoryPanel）は LaviContext::Get().gkManager を参照する。
///
/// MediaCaptureDemoSystem からの切り出し（ECS 分解）。
/// 注: 本フェーズ(8b-2)では既存 GatekeeperManager を所有して駆動するだけ。後続で内部を
///     CapturePromptComponent 消費＋キャプチャ評価 System へ分解する。
/// </summary>
class GatekeeperSystem : public OriGine::ISystem {
public:
    GatekeeperSystem();
    ~GatekeeperSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<GatekeeperManager> gkManager_;
};
