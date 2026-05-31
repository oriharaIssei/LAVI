#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

struct llama_model;
struct llama_context;

using LocalLLMCallback = std::function<void(const std::string& token)>;

/// <summary>会話履歴の 1 メッセージ（role: "user" / "assistant" / "system"）。</summary>
struct LocalChatMessage {
    std::string role;
    std::string content;
};

class LocalLLM {
public:
    LocalLLM();
    ~LocalLLM();

    bool LoadModel(const std::string& modelPath, int nGpuLayers = 99, int contextSize = 4096);
    void UnloadModel();
    bool IsModelLoaded() const;

    void SetMaxTokens(int maxTokens);

    /// <summary>reasoning モデルの &lt;think&gt;...&lt;/think&gt; を出力から除去する（既定 true）。</summary>
    void SetStripThinkTags(bool v) { stripThink_ = v; }
    /// <summary>思考を無効化する（Qwen3 等に /no_think を注入。既定 true＝高速・思考なし）。</summary>
    void SetDisableThinking(bool v) { disableThinking_ = v; }
    bool IsThinkingDisabled() const { return disableThinking_; }

    std::string Generate(const std::string& prompt);
    std::string GenerateChat(const std::string& systemPrompt, const std::string& userPrompt);
    std::future<std::string> GenerateAsync(const std::string& prompt);
    std::future<std::string> GenerateAsync(const std::string& prompt, LocalLLMCallback callback);

    /// <summary>
    /// 会話履歴（systemPrompt + messages）をチャットテンプレートで整形し、
    /// トークンを逐次 callback しながら非同期生成する。
    /// </summary>
    std::future<std::string> GenerateChatStreamAsync(const std::string& systemPrompt,
                                                     const std::vector<LocalChatMessage>& messages,
                                                     LocalLLMCallback callback);

    bool HasChatTemplate() const;

    bool IsProcessing() const { return isProcessing_.load(); }
    void Cancel();

private:
    std::string FormatChat(const std::string& systemPrompt,
                           const std::vector<LocalChatMessage>& messages) const;

    // 思考無効化のため systemPrompt に /no_think を付与する
    std::string ApplyNoThink(const std::string& systemPrompt) const;
    // 文字列全体から <think>...</think> を除去する（同期パス用）
    static std::string StripThink(const std::string& text);

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    int contextSize_ = 4096;
    int maxTokens_ = 512;

    std::atomic<bool> isProcessing_{false};
    std::atomic<bool> cancelRequested_{false};
    mutable std::mutex mutex_;

    bool stripThink_ = true;
    bool disableThinking_ = true;

    // 直前のストリーミング生成で KV に入っているトークン列（プレフィックス再利用用）。
    // llama_token は int32_t。ヘッダで llama.h を含めないため int32_t で保持する。
    std::vector<int32_t> cachedTokens_;
};
