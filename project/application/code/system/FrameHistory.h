#pragma once

#include "component/CapturedFrame.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

/// <summary>
/// 直近キャプチャフレームの時系列リング。プロデューサ（WebCamera/ScreenCaptureSystem）が新フレームを
/// Push し、消費者（AutoObserve/Vision/会話）が Sample(K) で「動画的に解釈」するための時系列 K 枚を取得する。
/// メモリと推論コストを抑えるため、保持は一定間隔(retainFps)に間引き、時間窓＋枚数で trim し、
/// 大きいフレーム（4K 画面等）は縮小コピーして保持する（カメラ等の小フレームはコピーせず共有）。
/// スナップショットは不変 shared_ptr なので Sample 結果は安全に非同期へ渡せる。
/// 単一スレッド（メインスレッドの System Update）からのみ触る前提。
/// </summary>
class FrameHistory {
public:
    void SetRetainFps(float _fps) { retainInterval_ = (_fps > 0.0f) ? 1.0f / _fps : 0.0f; }
    void SetWindowSeconds(float _s) { windowSec_ = (_s > 0.0f) ? _s : 0.0f; }
    void SetMaxFrames(int _n) { maxFrames_ = (_n < 1) ? 1 : _n; }
    void SetMaxLongSide(uint32_t _px) { maxLongSide_ = _px; } // 0 = 縮小しない

    /// 最新スナップショットを履歴へ取り込む（間引き・縮小・trim 込み）。
    void Push(const std::shared_ptr<const CapturedFrame>& _frame);

    /// 時間窓内の保持フレームから最大 _count 枚を時系列順（古→新）に均等サンプルする。
    std::vector<std::shared_ptr<const CapturedFrame>> Sample(int _count) const;

    void Clear() {
        frames_.clear();
        lastRetainTime_ = -1.0;
    }
    bool Empty() const { return frames_.empty(); }

private:
    std::deque<std::shared_ptr<const CapturedFrame>> frames_; // 古い順
    double lastRetainTime_ = -1.0;

    float retainInterval_ = 0.25f; // 最短保持間隔（秒）＝既定 4fps
    float windowSec_      = 2.0f;  // 保持する時間窓
    int maxFrames_        = 8;     // 枚数上限（メモリ上限）
    uint32_t maxLongSide_ = 1280;  // これを超える長辺は縮小して保持（0=縮小しない）
};
