#pragma once

#include <memory>
#include <string>
#include <vector>

class SentenceEmbedding {
public:
    SentenceEmbedding();
    ~SentenceEmbedding();

    SentenceEmbedding(const SentenceEmbedding&) = delete;
    SentenceEmbedding& operator=(const SentenceEmbedding&) = delete;

    bool LoadModel(const std::wstring& modelPath, const std::string& vocabPath, bool useGpu = false);
    void Unload();
    bool IsReady() const;

    std::vector<float> Encode(const std::string& text) const;

    static float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

    int EmbeddingDim() const;
    const std::string& LastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
