#pragma once

#include "system/ISystem.h"

#include <memory>

class LocationProvider;

/// <summary>
/// 現在地（Windows 位置情報→逆ジオコーディング）の取得を担う ECS システム。
/// LocationProvider を所有し、初期化時にバックグラウンド取得を開始、
/// LaviContext（共有状態）にポインタを公開する。消費側は LaviContext::Get().location を参照する。
///
/// MediaCaptureDemoSystem からの切り出し第1号（ECS 分解のストラングラー移行）。
/// </summary>
class LocationSystem : public OriGine::ISystem {
public:
    LocationSystem();
    ~LocationSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // 取得はバックグラウンドスレッド。毎フレーム処理は不要。

private:
    std::unique_ptr<LocationProvider> location_;
};
