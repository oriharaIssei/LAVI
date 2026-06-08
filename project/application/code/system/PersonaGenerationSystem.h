#pragma once

#include "system/ISystem.h"

#include "LLMClient.h"

#include <future>
#include <memory>
#include <string>

/// <summary>
/// ペルソナ生成（説明文 → システムプロンプト）の本番 ECS システム。
/// LLM 設定ページのテキスト入力（actionId="llm.persona.generate"）の確定で、
/// 説明文を Claude（haiku）に渡して LAVI 用システムプロンプトを生成し、
/// 成功時に LaviContext::Get().config.llmSystemPrompt を更新＋AppConfig へ永続化する。
/// 以後の会話（GatekeeperManager::Dispatch が config.llmSystemPrompt を使用）へ即時反映。
///
/// 旧 LLMChatPanel::GeneratePersonaPrompt（DEBUG 限定）の ECS 化。
/// </summary>
class PersonaGenerationSystem : public OriGine::ISystem {
public:
    PersonaGenerationSystem();
    ~PersonaGenerationSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    void StartGeneration(const std::string& description);

    std::unique_ptr<LLMClient> client_;
    std::future<LLMResponse> future_;
    bool generating_ = false;
    std::string status_; // 入力欄プレースホルダへ反映する状態文字列
};
