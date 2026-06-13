#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

class OnnxInferenceEngine;

namespace cv {
class CascadeClassifier;
}

// FER+ の出力ラベル順 (emotion-ferplus-8.onnx の出力チャンネル順に一致)
enum class Emotion {
    Neutral,
    Happiness,
    Surprise,
    Sadness,
    Anger,
    Disgust,
    Fear,
    Contempt,
    Count
};

struct EmotionResult {
    bool faceDetected = false;
    std::array<float, 8> scores{};  // softmax 後の確率
    Emotion dominant = Emotion::Neutral;
    float confidence = 0.0f;
    bool changed = false;  // 前回採用された支配的感情から変化したか

    // 検出された顔の矩形 (フレーム座標系)
    int faceX = 0, faceY = 0, faceW = 0, faceH = 0;

    // この結果を算出した CapturedFrame の seq（顔矩形とフレームの整合確認用）。
    uint64_t frameSeq = 0;
};

// カメラ映像のゲートキーパー。
// OpenCV Haar で顔を検出し、FER+ (ONNX) で表情を分類、
// 感情変化を検出したら Claude API へのエスカレーション判断材料を返す。
class CameraGatekeeper {
public:
    CameraGatekeeper();
    ~CameraGatekeeper();

    CameraGatekeeper(const CameraGatekeeper&) = delete;
    CameraGatekeeper& operator=(const CameraGatekeeper&) = delete;

    // ferModelPath: FER+ の .onnx パス / haarCascadePath: Haar XML パス
    bool Initialize(const std::wstring& ferModelPath,
                    const std::string& haarCascadePath,
                    bool useGpu = false);
    bool IsReady() const;

    // BGRA フレームを評価 (顔検出 → FER+ → 感情判定)。
    EmotionResult Evaluate(const uint8_t* bgra, uint32_t width, uint32_t height);

    // この結果を Claude API へエスカレーションすべきか。
    bool ShouldTrigger(const EmotionResult& r) const;

    void SetConfidenceThreshold(float t) { confidenceThreshold_ = t; }
    float ConfidenceThreshold() const { return confidenceThreshold_; }

    // 顔検出 (Haar detectMultiScale) のパラメータ
    void SetDetectionParams(double scaleFactor, int minNeighbors, int minFaceSize);

    // トリガー安定化: 同じ感情が confirmFrames 回連続したら確定して trigger を出す (1=即時)
    void SetConfirmFrames(int n) { confirmFrames_ = (n < 1) ? 1 : n; }

    // 無表情 (neutral) への変化ではトリガーしない
    void SetIgnoreNeutral(bool b) { ignoreNeutral_ = b; }

    // neutral 抑制 (0..1): 表情が硬く neutral が出続ける場合に neutral を割り引いて
    // 他の感情が支配的になりやすくする。argmax 選択時のみ適用 (表示スコアは生のまま)。
    void SetNeutralBias(float b) { neutralBias_ = (b < 0.0f) ? 0.0f : (b > 1.0f ? 1.0f : b); }

    void ResetState();

    const std::string& LastError() const { return lastError_; }

    static const char* EmotionName(Emotion e);

private:
    std::unique_ptr<OnnxInferenceEngine> fer_;
    std::unique_ptr<cv::CascadeClassifier> faceCascade_;

    float confidenceThreshold_ = 0.5f;

    // 顔検出パラメータ
    double scaleFactor_ = 1.1;
    int minNeighbors_   = 5;
    int minFaceSize_    = 80;

    // トリガー安定化
    int confirmFrames_  = 1;
    bool ignoreNeutral_ = false;
    float neutralBias_  = 0.0f;

    // 確定済みの支配的感情
    Emotion lastDominant_ = Emotion::Neutral;
    bool hasLast_ = false;

    // デバウンス用の候補状態
    Emotion candidate_   = Emotion::Neutral;
    int candidateCount_  = 0;
    bool hasCandidate_   = false;

    std::string lastError_;
};
