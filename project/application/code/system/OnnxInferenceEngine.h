#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ONNX Runtime の汎用推論ラッパ。
// ヘッダに onnxruntime のヘッダを漏らさないよう PImpl で隠蔽する。
// FER+ 等の単入力・単出力モデルから、複数 IO のモデルまで扱える。

struct OnnxTensor {
    std::vector<float> data;     // 行優先 (row-major) のフラットな値
    std::vector<int64_t> shape;  // 例: {1, 1, 64, 64}
};

struct OnnxTensorInt64 {
    std::vector<int64_t> data;
    std::vector<int64_t> shape;
};

class OnnxInferenceEngine {
public:
    OnnxInferenceEngine();
    ~OnnxInferenceEngine();

    OnnxInferenceEngine(const OnnxInferenceEngine&) = delete;
    OnnxInferenceEngine& operator=(const OnnxInferenceEngine&) = delete;

    // モデルを読み込む。useGpu=true で CUDA Execution Provider を試みる
    // (失敗時は CPU にフォールバック)。
    bool LoadModel(const std::wstring& modelPath, bool useGpu = false);
    void UnloadModel();
    bool IsModelLoaded() const;

    // 単入力・単出力モデル向けの簡易版。
    bool Run(const OnnxTensor& input, OnnxTensor& output);

    // 複数 IO 向け。inputs はモデルの入力順に並べる。
    bool Run(const std::vector<OnnxTensor>& inputs, std::vector<OnnxTensor>& outputs);

    // int64 入力 → float 出力 (Transformerトークン入力向け)
    bool Run(const std::vector<OnnxTensorInt64>& inputs, std::vector<OnnxTensor>& outputs);

    const std::vector<std::string>& InputNames() const;
    const std::vector<std::string>& OutputNames() const;

    const std::string& LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
