#include "SentenceEmbedding.h"
#include "OnnxInferenceEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <sstream>
#include <unordered_map>

// BPE vocab: token text -> token id
// ファイル形式: 1行 = "token\tid\n" (UTF-8)
// 特殊トークン: <s>=0, </s>=2 (XLM-RoBERTa)
struct BpeVocab {
    std::unordered_map<std::string, int> tokenToId;
    int bosId = 0;   // <s>
    int eosId = 2;   // </s>
    int unkId = 3;   // <unk>

    bool Load(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            auto tab = line.rfind('\t');
            if (tab == std::string::npos) continue;
            std::string token = line.substr(0, tab);
            int id = std::atoi(line.substr(tab + 1).c_str());
            tokenToId[token] = id;
        }
        if (auto it = tokenToId.find("<s>"); it != tokenToId.end()) bosId = it->second;
        if (auto it = tokenToId.find("</s>"); it != tokenToId.end()) eosId = it->second;
        if (auto it = tokenToId.find("<unk>"); it != tokenToId.end()) unkId = it->second;
        return !tokenToId.empty();
    }

    int GetId(const std::string& token) const {
        auto it = tokenToId.find(token);
        return (it != tokenToId.end()) ? it->second : unkId;
    }
};

// UTF-8 文字列を1文字ずつ分割
static std::vector<std::string> Utf8Chars(const std::string& s) {
    std::vector<std::string> chars;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        if (i + len > s.size()) len = 1;
        chars.push_back(s.substr(i, len));
        i += len;
    }
    return chars;
}

// SentencePiece BPE: 最もスコアの高いペアを繰り返しマージ
// vocab にマージ後のトークンがあればマージする貪欲法
static std::vector<int> TokenizeBpe(const std::string& text, const BpeVocab& vocab) {
    // SentencePiece の ▁ (U+2581) プレフィックス付き
    static const char kSpacePrefix[] = "\xE2\x96\x81"; // ▁

    std::vector<int> ids;
    ids.push_back(vocab.bosId);

    // 空白で分割（先頭トークンにも ▁ を付ける）
    std::istringstream iss(text);
    std::string word;
    bool first = true;
    while (iss >> word) {
        std::string prefixed = kSpacePrefix + word;

        // まず全体が vocab にあるか確認
        auto wholeIt = vocab.tokenToId.find(prefixed);
        if (wholeIt != vocab.tokenToId.end()) {
            ids.push_back(wholeIt->second);
            first = false;
            continue;
        }

        // UTF-8文字に分割して貪欲 longest match
        auto chars = Utf8Chars(prefixed);
        size_t pos = 0;
        while (pos < chars.size()) {
            int bestLen = 0;
            int bestId = vocab.unkId;
            // 長い方から試す
            for (size_t end = chars.size(); end > pos; --end) {
                std::string sub;
                for (size_t k = pos; k < end; ++k) sub += chars[k];
                auto it = vocab.tokenToId.find(sub);
                if (it != vocab.tokenToId.end()) {
                    bestLen = static_cast<int>(end - pos);
                    bestId = it->second;
                    break;
                }
            }
            if (bestLen == 0) {
                ids.push_back(vocab.unkId);
                pos++;
            } else {
                ids.push_back(bestId);
                pos += bestLen;
            }
        }
        first = false;
    }

    ids.push_back(vocab.eosId);
    return ids;
}

// モデルの実際の入力名に合わせて int64 入力を構築する。
// multilingual-e5-small の ONNX エクスポートは input_ids / attention_mask に加え
// token_type_ids(全0) を要求する版があり、入力数が合わないと OnnxInferenceEngine が
// "input count mismatch" を返して推論に失敗する（＝埋め込み未ロードの原因）。
static std::vector<OnnxTensorInt64> BuildTokenInputs(const OnnxInferenceEngine& engine,
                                                     const std::vector<int64_t>& ids,
                                                     const std::vector<int64_t>& mask) {
    const int64_t seqLen = static_cast<int64_t>(ids.size());
    std::vector<OnnxTensorInt64> inputs;
    for (const auto& n : engine.InputNames()) {
        if (n.find("mask") != std::string::npos) {
            inputs.push_back({mask, {1, seqLen}});
        } else if (n.find("type") != std::string::npos || n.find("segment") != std::string::npos) {
            inputs.push_back({std::vector<int64_t>(ids.size(), 0), {1, seqLen}}); // token_type_ids=0
        } else {
            inputs.push_back({ids, {1, seqLen}}); // input_ids
        }
    }
    if (inputs.empty()) { // 入力名が取得できない場合のフォールバック（input_ids, attention_mask）
        inputs.push_back({ids, {1, seqLen}});
        inputs.push_back({mask, {1, seqLen}});
    }
    return inputs;
}

struct SentenceEmbedding::Impl {
    OnnxInferenceEngine engine;
    BpeVocab vocab;
    mutable std::string lastError;
    int embDim = 0;
};

SentenceEmbedding::SentenceEmbedding()
    : impl_(std::make_unique<Impl>()) {}

SentenceEmbedding::~SentenceEmbedding() = default;

bool SentenceEmbedding::LoadModel(const std::wstring& modelPath, const std::string& vocabPath, bool useGpu) {
    if (!impl_->vocab.Load(vocabPath)) {
        impl_->lastError = "failed to load vocab: " + vocabPath;
        return false;
    }

    if (!impl_->engine.LoadModel(modelPath, useGpu)) {
        impl_->lastError = "failed to load model: " + impl_->engine.LastError();
        return false;
    }

    // テスト推論で embedding 次元を取得
    auto testIds = TokenizeBpe("test", impl_->vocab);
    std::vector<int64_t> inputIds(testIds.begin(), testIds.end());
    std::vector<int64_t> attentionMask(inputIds.size(), 1);

    auto testInputs = BuildTokenInputs(impl_->engine, inputIds, attentionMask);

    std::vector<OnnxTensor> outputs;
    if (!impl_->engine.Run(testInputs, outputs) || outputs.empty()) {
        impl_->lastError = "test inference failed: " + impl_->engine.LastError();
        return false;
    }

    // 出力 shape から embedding 次元を取得
    // multilingual-e5-small: shape = [1, seq_len, 384]
    if (outputs[0].shape.size() >= 2) {
        impl_->embDim = static_cast<int>(outputs[0].shape.back());
    } else {
        impl_->embDim = static_cast<int>(outputs[0].data.size());
    }

    return true;
}

void SentenceEmbedding::Unload() {
    impl_->engine.UnloadModel();
    impl_->vocab.tokenToId.clear();
    impl_->embDim = 0;
}

bool SentenceEmbedding::IsReady() const {
    return impl_->engine.IsModelLoaded() && !impl_->vocab.tokenToId.empty();
}

std::vector<float> SentenceEmbedding::Encode(const std::string& text) const {
    if (!IsReady()) return {};

    // e5 モデルは "query: " プレフィックスで検索用途に最適化
    std::string prefixed = "query: " + text;
    auto tokenIds = TokenizeBpe(prefixed, impl_->vocab);

    std::vector<int64_t> inputIds(tokenIds.begin(), tokenIds.end());
    std::vector<int64_t> attentionMask(inputIds.size(), 1);

    auto inputs = BuildTokenInputs(impl_->engine, inputIds, attentionMask);

    std::vector<OnnxTensor> outputs;
    if (!impl_->engine.Run(inputs, outputs) || outputs.empty()) {
        return {};
    }

    // Mean pooling: attention_mask で重み付き平均
    // 出力 shape: [1, seq_len, dim]
    const auto& out = outputs[0];
    if (out.shape.size() < 3) return {};

    const int dim = static_cast<int>(out.shape[2]);
    const int seq = static_cast<int>(out.shape[1]);

    std::vector<float> embedding(dim, 0.0f);
    float totalWeight = 0.0f;
    for (int s = 0; s < seq; ++s) {
        float w = (s < static_cast<int>(attentionMask.size())) ? static_cast<float>(attentionMask[s]) : 0.0f;
        totalWeight += w;
        for (int d = 0; d < dim; ++d) {
            embedding[d] += out.data[s * dim + d] * w;
        }
    }
    if (totalWeight > 0.0f) {
        for (auto& v : embedding) v /= totalWeight;
    }

    // L2 正規化
    float norm = 0.0f;
    for (auto v : embedding) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-12f) {
        for (auto& v : embedding) v /= norm;
    }

    return embedding;
}

float SentenceEmbedding::CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    float denom = std::sqrt(na) * std::sqrt(nb);
    return (denom > 1e-12f) ? dot / denom : 0.0f;
}

int SentenceEmbedding::EmbeddingDim() const {
    return impl_->embDim;
}

const std::string& SentenceEmbedding::LastError() const {
    return impl_->lastError;
}
