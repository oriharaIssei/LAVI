#pragma once

#include "VisionAnalyzer.h" // VisionResult を共有（クラウド版と同じ戻り型）

#include <atomic>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;
struct mtmd_context;

/// <summary>
/// ローカル画像解析（Qwen2.5-VL 等）。llama.cpp + libmtmd をプロセス内で使用する。
/// クラウドの `VisionAnalyzer` と同じ `VisionResult` を返し、`AnalyzeAsync(bgra,w,h)` で
/// BGRA フレーム（カメラ/画面）を説明文に変換する。GGUF 本体 + mmproj(vision projector) が必要。
///
/// 所有・公開は VisionSystem（ECS）。Qwen2.5-VL 統合（`mem:feedback_native_deps_mt_static`）。
/// </summary>
class LocalVisionAnalyzer {
public:
    LocalVisionAnalyzer();
    ~LocalVisionAnalyzer();

    void SetModelPaths(const std::string& modelPath, const std::string& mmprojPath);
    void SetSystemPrompt(const std::string& s) { systemPrompt_ = s; }
    void SetPrompt(const std::string& p) { prompt_ = p; }
    void SetMaxTokens(int n) { maxTokens_ = n; }
    void SetGpuLayers(int n) { gpuLayers_ = n; }

    bool LoadModel();        // llama 本体 + mtmd(clip) をロード（同期・重い）
    void UnloadModel();
    bool IsModelLoaded() const;

    // 単一フレーム（BGRA）。
    std::future<VisionResult> AnalyzeAsync(const uint8_t* bgra, uint32_t width, uint32_t height);
    VisionResult Analyze(const uint8_t* bgra, uint32_t width, uint32_t height);

    // 複数フレーム（BGRA）。カメラ＋複数スクリーン等をまとめて 1 回の推論に渡す。
    struct Frame {
        const uint8_t* bgra;
        uint32_t width;
        uint32_t height;
    };
    std::future<VisionResult> AnalyzeAsync(const std::vector<Frame>& frames);

    bool IsAnalyzing() const { return analyzing_.load(); }
    void Cancel() { cancel_.store(true); }

private:
    std::string FormatPrompt(const std::string& userContentWithMarker) const;
    // RGB 済みフレーム群を入力に推論（画像 0 枚＝テキストのみも可）。
    struct RgbFrame { std::vector<uint8_t> rgb; uint32_t width; uint32_t height; };
    VisionResult AnalyzeRgbInternal(std::vector<RgbFrame> frames);

    std::string modelPath_;
    std::string mmprojPath_;
    std::string systemPrompt_;
    std::string prompt_ = "この画像に写っているものを日本語で説明してください。";
    int maxTokens_ = 256;
    int gpuLayers_ = 99;
    int ctxSize_   = 4096;

    llama_model*   model_ = nullptr;
    llama_context* lctx_  = nullptr;
    mtmd_context*  mctx_  = nullptr;

    std::atomic<bool> analyzing_{false};
    std::atomic<bool> cancel_{false};
    mutable std::mutex mutex_;
};
