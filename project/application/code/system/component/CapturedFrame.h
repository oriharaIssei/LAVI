#pragma once

#include <cstdint>
#include <vector>

/// <summary>
/// 1 デバイス・1 フレーム分の不変スナップショット（依存の無い POD）。
/// プロデューサ（WebCameraSystem/ScreenCaptureSystem）が新フレームごとに 1 個生成し、
/// shared_ptr&lt;const&gt; で共有所有する。消費者・ワーカーは shared_ptr をコピーして掴むだけで
/// バッファをコピーせず寿命が延びるため、非同期評価からも安全に読める（refcount はアトミック）。
/// SharedMediaContext から engine ECS ヘッダを引かずに参照できるよう Component とは別ヘッダにしている。
/// </summary>
struct CapturedFrame {
    std::vector<uint8_t> pixels; // BGRA
    uint32_t width  = 0;
    uint32_t height = 0;
    uint64_t seq    = 0;   // 単調増加。評価の重複判定・フレーム整合に使う
    double time     = 0.0; // 取得時刻（秒）
    int source      = 0;   // 0=Camera, 1=Screen
};
