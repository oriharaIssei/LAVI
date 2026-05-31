#pragma once

#include <future>
#include <string>
#include <utility>
#include <vector>

/// <summary>
/// 音声認識(ASR)結果を LLM で「校正」するヘルパー。
/// 明らかな誤認識・固有名詞・同音異義語・句読点・言い淀みのみ直し、
/// 語・意味・意図は変えない（意訳はしない）。
/// 固有名詞リストと直近の会話文脈をプロンプトに注入して曖昧さを減らす。
/// </summary>
class TranscriptRefiner {
public:
    void SetApiKey(const std::string& apiKey) { apiKey_ = apiKey; }
    /// <summary>使用モデル。空なら LLMClient の既定（高速モデル）を使う。</summary>
    void SetModel(const std::string& model) { model_ = model; }
    void SetVocabulary(const std::vector<std::string>& names) { vocabulary_ = names; }
    void SetContext(const std::string& recentContext) { context_ = recentContext; }
    /// <summary>過去の校正例（誤→正）。few-shot として校正プロンプトに注入する。</summary>
    void SetExamples(const std::vector<std::pair<std::string, std::string>>& examples) { examples_ = examples; }

    bool IsConfigured() const { return !apiKey_.empty(); }

    /// <summary>
    /// 校正を非同期で実行する。成功時は校正後テキスト、失敗時は空文字列を返す。
    /// </summary>
    std::future<std::string> RefineAsync(const std::string& rawText) const;

private:
    std::string BuildUserMessage(const std::string& rawText) const;

    std::string apiKey_;
    std::string model_; // 空なら LLMClient 既定（Haiku）
    std::vector<std::string> vocabulary_;
    std::string context_;
    std::vector<std::pair<std::string, std::string>> examples_;
};
