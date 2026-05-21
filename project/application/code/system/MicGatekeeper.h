#pragma once

#include <string>
#include <vector>

// マイクのゲートキーパー。
// 既存の WhisperTranscriber が出力する転写テキスト (ctx->transcribedText) を受け取り、
// 登録キーワード (ウェイクワード・特定フレーズ) と照合する。
// 新規に追加された部分のみを走査するため、同じ発話で何度もトリガーしない。

struct MicGateResult {
    bool triggered = false;
    std::vector<std::string> matched;  // 一致したキーワード
    std::string newText;               // 今回新たに増えたテキスト (表示用)
};

class MicGatekeeper {
public:
    void SetKeywords(const std::vector<std::string>& kws);
    void SetCaseSensitive(bool b) { caseSensitive_ = b; }

    // transcribedText 全文を渡す。前回からの差分のみ走査する。
    MicGateResult Evaluate(const std::string& transcribedText);

    void ResetState();

    const std::vector<std::string>& Keywords() const { return keywords_; }

private:
    static std::string ToLowerAscii(const std::string& s);

    std::vector<std::string> keywords_;
    bool caseSensitive_ = false;
    std::string lastText_;
};
