#include "KnowledgeSystem.h"

#include "SentenceEmbedding.h"
#include "KnowledgeBase.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"

#include <filesystem>
#include <fstream>

KnowledgeSystem::KnowledgeSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

KnowledgeSystem::~KnowledgeSystem() = default;

void KnowledgeSystem::Initialize() {
    // Sentence Embedding (存在すれば読み込み)
    const std::filesystem::path embDir = "application/resource/embedding";
    const std::filesystem::path modelPath = embDir / "model.onnx";
    const std::filesystem::path vocabPath = embDir / "vocab.txt";
    // 一時診断: 埋め込みロードの可否を lavi_trace.log に記録する（原因切り分け後に除去）。
    {
        std::ofstream tr("lavi_trace.log", std::ios::app);
        if(tr) tr << "Embedding: model_exists=" << std::filesystem::exists(modelPath)
                  << " vocab_exists=" << std::filesystem::exists(vocabPath)
                  << " (" << embDir.string() << ")\n";
    }
    if(std::filesystem::exists(modelPath) && std::filesystem::exists(vocabPath)){
        embedding_ = std::make_unique<SentenceEmbedding>();
        if(embedding_->LoadModel(modelPath.wstring(),vocabPath.string())){
            LaviContext::Get().embedding = embedding_.get(); // 共有状態に公開

            // 知識ベース RAG: 埋め込み共有 + SQLite。起動時に knowledge/ フォルダを取り込む。
            knowledgeBase_ = std::make_unique<KnowledgeBase>();
            if(knowledgeBase_->Initialize("application/resource/knowledge/knowledge.db", embedding_.get())){
                knowledgeBase_->ImportFolder("application/resource/knowledge");
                LaviContext::Get().knowledgeBase = knowledgeBase_.get(); // 消費側は LaviContext 経由
            } else{
                std::ofstream tr("lavi_trace.log", std::ios::app);
                if(tr) tr << "KnowledgeBase Initialize FAILED (knowledge.db open?)\n";
                knowledgeBase_.reset();
            }
        } else{
            std::ofstream tr("lavi_trace.log", std::ios::app);
            if(tr) tr << "Embedding LoadModel FAILED: " << embedding_->LastError() << "\n";
            embedding_.reset();
        }
    }
}

void KnowledgeSystem::Finalize() {
    LaviContext::Get().knowledgeBase = nullptr;
    LaviContext::Get().embedding = nullptr;
    if(knowledgeBase_){ knowledgeBase_->Finalize(); knowledgeBase_.reset(); }
    embedding_.reset();
}
