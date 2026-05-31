#include "TranscriptRefiner.h"

#include "LLMClient.h"

namespace {

const char* kCorrectionSystemPrompt =
    "あなたは日本語の音声認識(ASR)結果を校正するエディタです。"
    "入力は音声認識の出力で、誤認識を含みます。"
    "明らかな認識誤り・同音異義語・固有名詞の誤字・句読点・言い淀み(えー、あの、などのフィラー)のみを修正してください。"
    "話者の語彙・意味・意図は変えないこと。言い換え・要約・情報の追加や削除はしないこと。"
    "判断できない箇所は推測で変えず原文のまま残すこと。"
    "出力は校正後の本文のみ。説明・引用符・前置き・ラベルは一切付けないこと。";

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

std::string TranscriptRefiner::BuildUserMessage(const std::string& rawText) const {
    std::string msg;
    if (!vocabulary_.empty()) {
        msg += "固有名詞候補(綴りの参考): ";
        for (size_t i = 0; i < vocabulary_.size(); ++i) {
            if (i > 0) msg += "、";
            msg += vocabulary_[i];
        }
        msg += "\n";
    }
    if (!examples_.empty()) {
        msg += "過去の校正例(誤→正、綴りの参考): ";
        for (size_t i = 0; i < examples_.size(); ++i) {
            if (i > 0) msg += "、";
            msg += "「" + examples_[i].first + "」→「" + examples_[i].second + "」";
        }
        msg += "\n";
    }
    if (!context_.empty()) {
        msg += "直近の会話文脈:\n" + context_ + "\n";
    }
    msg += "---\n音声認識結果:\n" + rawText + "\n---\n上記を校正した本文のみを出力してください:";
    return msg;
}

std::future<std::string> TranscriptRefiner::RefineAsync(const std::string& rawText) const {
    // スレッドへ値コピーして寿命・競合を避ける
    std::string apiKey = apiKey_;
    std::string model  = model_;
    std::string user   = BuildUserMessage(rawText);

    return std::async(std::launch::async, [apiKey, model, user]() -> std::string {
        if (apiKey.empty()) {
            return "";
        }
        LLMClient client;
        client.SetApiKey(apiKey);
        if (!model.empty()) {
            client.SetModel(model);
        }
        client.SetSystemPrompt(kCorrectionSystemPrompt);
        client.SetMaxTokens(1024);
        client.AddMessage("user", user);

        LLMResponse res = client.SendBlocking();
        if (!res.success) {
            return "";
        }
        return Trim(res.content);
    });
}
