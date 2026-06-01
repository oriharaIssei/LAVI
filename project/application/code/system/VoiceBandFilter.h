#pragma once

#include <cmath>
#include <cstdint>

/// <summary>
/// 声帯域（既定 300-3400Hz）バンドパス。RBJ Cookbook の biquad を
/// ハイパス + ローパスの 2 段カスケードで構成する。
/// 状態を保持するためコールバック間で同一インスタンスを使い回す（モノ前提）。
/// 雑音（低域の空調・ファン / 高域のヒス）を落とし、発話帯域のみで RMS を測るため。
/// </summary>
class VoiceBandFilter {
public:
    /// サンプルレートまたは帯域が変わったら係数を再計算する。
    void Configure(float sampleRate, float lowHz = 300.0f, float highHz = 3400.0f) {
        if (sampleRate <= 0.0f) return;
        if (sampleRate == sampleRate_ && lowHz == lowHz_ && highHz == highHz_) return;
        sampleRate_ = sampleRate;
        lowHz_      = lowHz;
        highHz_     = highHz;
        DesignHighpass(hp_, lowHz, sampleRate);
        DesignLowpass(lp_, highHz, sampleRate);
        Reset();
    }

    bool IsConfigured() const { return sampleRate_ > 0.0f; }

    void Reset() {
        hp_.z1 = hp_.z2 = 0.0f;
        lp_.z1 = lp_.z2 = 0.0f;
    }

    /// 1 サンプル処理（ハイパス → ローパス）。
    float Process(float x) {
        return lp_.Process(hp_.Process(x));
    }

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f; // 転置 Direct Form II の状態
        float Process(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kQ  = 0.70710678f; // Butterworth（最大平坦）

    static void DesignHighpass(Biquad& bq, float fc, float fs) {
        const float w0    = 2.0f * kPi * fc / fs;
        const float cosw0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * kQ);
        const float a0    = 1.0f + alpha;
        bq.b0 = ((1.0f + cosw0) * 0.5f) / a0;
        bq.b1 = (-(1.0f + cosw0)) / a0;
        bq.b2 = ((1.0f + cosw0) * 0.5f) / a0;
        bq.a1 = (-2.0f * cosw0) / a0;
        bq.a2 = (1.0f - alpha) / a0;
    }

    static void DesignLowpass(Biquad& bq, float fc, float fs) {
        const float w0    = 2.0f * kPi * fc / fs;
        const float cosw0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * kQ);
        const float a0    = 1.0f + alpha;
        bq.b0 = ((1.0f - cosw0) * 0.5f) / a0;
        bq.b1 = (1.0f - cosw0) / a0;
        bq.b2 = ((1.0f - cosw0) * 0.5f) / a0;
        bq.a1 = (-2.0f * cosw0) / a0;
        bq.a2 = (1.0f - alpha) / a0;
    }

    float  sampleRate_ = 0.0f;
    float  lowHz_      = 0.0f;
    float  highHz_     = 0.0f;
    Biquad hp_;
    Biquad lp_;
};
