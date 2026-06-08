#pragma once

#include "system/ISystem.h"

/// <summary>
/// テキストチャット入力を会話パイプラインへ橋渡しする ECS システム（Category=Initialize）。
/// UIActionRegistry にテキストアクション "chat.send" を登録し、ECS UI のテキスト入力欄が
/// 確定した文字列を受け取って、音声会話と同じ経路（ctx.transcribedText → gkManager->RespondToSpeech）
/// で応答を生成・発話させる。記憶・人格・知識 RAG・ローカル/クラウド LLM・VoiceVox を音声と完全共有する。
/// </summary>
class TextChatSystem : public OriGine::ISystem {
public:
    TextChatSystem();
    ~TextChatSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // 駆動は不要（アクション登録のみ）
};
