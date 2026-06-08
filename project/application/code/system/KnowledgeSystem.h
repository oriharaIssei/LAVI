#pragma once

#include "system/ISystem.h"

#include <memory>

class SentenceEmbedding;
class KnowledgeBase;

/// <summary>
/// 文埋め込み（SentenceEmbedding）と知識ベース RAG（KnowledgeBase）を担う ECS システム。
/// 両者を所有し、初期化時にモデル/DB を読み込んで LaviContext に公開する。
/// 消費側（LLMChatPanel / GatekeeperManager / 編集UI）は LaviContext::Get().knowledgeBase /
/// .embedding を参照する。
///
/// MediaCaptureDemoSystem からの切り出し（ECS 分解）。パターンは WebSearchSystem と同じ。
/// </summary>
class KnowledgeSystem : public OriGine::ISystem {
public:
    KnowledgeSystem();
    ~KnowledgeSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // RAG は要求時に実行。毎フレーム処理は不要。

private:
    std::unique_ptr<SentenceEmbedding> embedding_;
    std::unique_ptr<KnowledgeBase> knowledgeBase_;
};
