#include "CameraGatekeeper.h"

#include "OnnxInferenceEngine.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kFerSize = 64;  // FER+ 入力は 64x64 グレースケール

// FER+ 出力 (8ロジット) を softmax して argmax を取る
void Softmax(const std::vector<float>& logits, std::array<float, 8>& out) {
    const float maxv = *std::max_element(logits.begin(), logits.end());
    float sum = 0.0f;
    for (size_t i = 0; i < 8 && i < logits.size(); ++i) {
        const float e = std::exp(logits[i] - maxv);
        out[i] = e;
        sum += e;
    }
    if (sum > 0.0f) {
        for (auto& v : out) v /= sum;
    }
}
}  // namespace

CameraGatekeeper::CameraGatekeeper()
    : fer_(std::make_unique<OnnxInferenceEngine>()),
      faceCascade_(std::make_unique<cv::CascadeClassifier>()) {}

CameraGatekeeper::~CameraGatekeeper() = default;

bool CameraGatekeeper::Initialize(const std::wstring& ferModelPath,
                                  const std::string& haarCascadePath,
                                  bool useGpu) {
    lastError_.clear();
    if (!fer_->LoadModel(ferModelPath, useGpu)) {
        lastError_ = "FER+ model load failed: " + fer_->LastError();
        return false;
    }
    if (!faceCascade_->load(haarCascadePath)) {
        lastError_ = "Haar cascade load failed: " + haarCascadePath;
        return false;
    }
    return true;
}

bool CameraGatekeeper::IsReady() const {
    return fer_ && fer_->IsModelLoaded() && faceCascade_ && !faceCascade_->empty();
}

EmotionResult CameraGatekeeper::Evaluate(const uint8_t* bgra, uint32_t width, uint32_t height) {
    EmotionResult result;
    if (!IsReady() || !bgra || width == 0 || height == 0) {
        return result;
    }

    // BGRA バッファをコピーせず Mat 化 (読み取りのみ)。静的リンクなので単一ヒープで安全。
    cv::Mat frame(static_cast<int>(height), static_cast<int>(width), CV_8UC4,
                  const_cast<uint8_t*>(bgra));

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    cv::equalizeHist(gray, gray);

    std::vector<cv::Rect> faces;
    const int minSide = (std::max)(0, minFaceSize_);
    faceCascade_->detectMultiScale(gray, faces, scaleFactor_, minNeighbors_, 0,
                                   cv::Size(minSide, minSide));
    if (faces.empty()) {
        result.faceDetected = false;
        hasCandidate_ = false;  // 顔が消えたら投票をリセット
        return result;
    }
    result.faceDetected = true;

    // 最大の顔を採用
    const cv::Rect face = *std::max_element(
        faces.begin(), faces.end(),
        [](const cv::Rect& a, const cv::Rect& b) { return a.area() < b.area(); });

    result.faceX = face.x;
    result.faceY = face.y;
    result.faceW = face.width;
    result.faceH = face.height;

    cv::Mat face64;
    cv::resize(gray(face), face64, cv::Size(kFerSize, kFerSize));

    // FER+ 入力: float32 [1,1,64,64]、画素値は 0-255 のまま (正規化なし)
    OnnxTensor input;
    input.shape = {1, 1, kFerSize, kFerSize};
    input.data.resize(static_cast<size_t>(kFerSize) * kFerSize);
    for (int y = 0; y < kFerSize; ++y) {
        const uint8_t* row = face64.ptr<uint8_t>(y);
        for (int x = 0; x < kFerSize; ++x) {
            input.data[static_cast<size_t>(y) * kFerSize + x] = static_cast<float>(row[x]);
        }
    }

    OnnxTensor output;
    if (!fer_->Run(input, output) || output.data.size() < 8) {
        lastError_ = "FER+ inference failed: " + fer_->LastError();
        result.faceDetected = true;
        return result;
    }

    Softmax(output.data, result.scores);

    // argmax は neutral を割り引いた選択用スコアで取る (表示は生スコアのまま)
    std::array<float, 8> sel = result.scores;
    sel[static_cast<int>(Emotion::Neutral)] *= (1.0f - neutralBias_);

    int best = 0;
    for (int i = 1; i < 8; ++i) {
        if (sel[i] > sel[best]) best = i;
    }
    result.dominant = static_cast<Emotion>(best);
    result.confidence = result.scores[best];

    // 確信度が低いフレームは投票に使わない
    if (result.confidence < confidenceThreshold_) {
        return result;
    }

    // デバウンス: 同じ感情が confirmFrames_ 回連続したら確定
    if (!hasCandidate_ || result.dominant != candidate_) {
        candidate_ = result.dominant;
        candidateCount_ = 1;
        hasCandidate_ = true;
    } else {
        ++candidateCount_;
    }

    if (candidateCount_ >= confirmFrames_) {
        const bool isChange = !hasLast_ || (candidate_ != lastDominant_);
        if (isChange && !(ignoreNeutral_ && candidate_ == Emotion::Neutral)) {
            result.changed = true;
        }
        lastDominant_ = candidate_;
        hasLast_ = true;
    }
    return result;
}

bool CameraGatekeeper::ShouldTrigger(const EmotionResult& r) const {
    return r.faceDetected && r.changed && r.confidence >= confidenceThreshold_;
}

void CameraGatekeeper::SetDetectionParams(double scaleFactor, int minNeighbors, int minFaceSize) {
    scaleFactor_ = (scaleFactor > 1.0) ? scaleFactor : 1.05;
    minNeighbors_ = (minNeighbors < 0) ? 0 : minNeighbors;
    minFaceSize_ = (minFaceSize < 0) ? 0 : minFaceSize;
}

void CameraGatekeeper::ResetState() {
    hasLast_ = false;
    lastDominant_ = Emotion::Neutral;
    hasCandidate_ = false;
    candidateCount_ = 0;
}

const char* CameraGatekeeper::EmotionName(Emotion e) {
    switch (e) {
        case Emotion::Neutral:   return "neutral";
        case Emotion::Happiness: return "happiness";
        case Emotion::Surprise:  return "surprise";
        case Emotion::Sadness:   return "sadness";
        case Emotion::Anger:     return "anger";
        case Emotion::Disgust:   return "disgust";
        case Emotion::Fear:      return "fear";
        case Emotion::Contempt:  return "contempt";
        default:                 return "unknown";
    }
}
