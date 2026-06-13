#include "FrameHistory.h"

namespace {
// BGRA を最近傍で縮小したコピーを作る（長辺を _maxLong 以下に）。
// pixels は width*4 のタイトパッキング（producer の CapturedFrame 既定）を前提。
std::shared_ptr<const CapturedFrame> DownscaleIfNeeded(
    const std::shared_ptr<const CapturedFrame>& _src, uint32_t _maxLong) {
    if (!_src) return _src;
    const uint32_t w = _src->width;
    const uint32_t h = _src->height;
    const uint32_t longSide = (w > h) ? w : h;
    if (_maxLong == 0 || longSide == 0 || longSide <= _maxLong) {
        return _src; // 縮小不要＝共有（コピーなし）
    }

    const double scale = static_cast<double>(_maxLong) / static_cast<double>(longSide);
    uint32_t nw = static_cast<uint32_t>(w * scale);
    uint32_t nh = static_cast<uint32_t>(h * scale);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    if (_src->pixels.size() < static_cast<size_t>(w) * h * 4) {
        return _src; // 想定外のサイズはそのまま
    }

    auto out    = std::make_shared<CapturedFrame>();
    out->width  = nw;
    out->height = nh;
    out->seq    = _src->seq;
    out->time   = _src->time;
    out->source = _src->source;
    out->pixels.resize(static_cast<size_t>(nw) * nh * 4);

    const uint8_t* s = _src->pixels.data();
    uint8_t* d       = out->pixels.data();
    for (uint32_t y = 0; y < nh; ++y) {
        uint32_t sy = static_cast<uint32_t>(y / scale);
        if (sy >= h) sy = h - 1;
        const uint8_t* srow = s + static_cast<size_t>(sy) * w * 4;
        uint8_t* drow       = d + static_cast<size_t>(y) * nw * 4;
        for (uint32_t x = 0; x < nw; ++x) {
            uint32_t sx = static_cast<uint32_t>(x / scale);
            if (sx >= w) sx = w - 1;
            const uint8_t* sp = srow + static_cast<size_t>(sx) * 4;
            uint8_t* dp       = drow + static_cast<size_t>(x) * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }
    return out;
}
} // namespace

void FrameHistory::Push(const std::shared_ptr<const CapturedFrame>& _frame) {
    if (!_frame || _frame->pixels.empty() || _frame->width == 0 || _frame->height == 0) {
        return;
    }
    const double now = _frame->time;

    // 一定間隔に間引く（高 fps ソースで履歴が溢れるのを防ぐ）。
    if (lastRetainTime_ >= 0.0 && retainInterval_ > 0.0f &&
        (now - lastRetainTime_) < static_cast<double>(retainInterval_)) {
        return;
    }
    lastRetainTime_ = now;

    frames_.push_back(DownscaleIfNeeded(_frame, maxLongSide_));

    // 時間窓より古いものを破棄（最新フレーム時刻基準）。
    if (windowSec_ > 0.0f) {
        const double cutoff = now - static_cast<double>(windowSec_);
        while (!frames_.empty() && frames_.front()->time < cutoff) {
            frames_.pop_front();
        }
    }
    // 枚数上限（メモリ保護）。
    while (static_cast<int>(frames_.size()) > maxFrames_) {
        frames_.pop_front();
    }
}

std::vector<std::shared_ptr<const CapturedFrame>> FrameHistory::Sample(int _count) const {
    std::vector<std::shared_ptr<const CapturedFrame>> out;
    if (frames_.empty() || _count < 1) {
        return out;
    }
    const int n = static_cast<int>(frames_.size());
    if (n <= _count) {
        out.assign(frames_.begin(), frames_.end()); // 全部（古→新）
        return out;
    }
    // [0, n-1] を _count 個で均等サンプル（端点を含む、古→新）。
    out.reserve(_count);
    int lastIdx = -1;
    for (int i = 0; i < _count; ++i) {
        int idx = (_count == 1) ? (n - 1)
                                : static_cast<int>(static_cast<double>(i) * (n - 1) / (_count - 1) + 0.5);
        if (idx <= lastIdx) idx = lastIdx + 1; // 重複回避
        if (idx >= n) break;
        out.push_back(frames_[idx]);
        lastIdx = idx;
    }
    return out;
}
