#include "TextChatSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "GatekeeperManager.h"
#include "system/ui/UIRegistry.h"

TextChatSystem::TextChatSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

TextChatSystem::~TextChatSystem() = default;

void TextChatSystem::Initialize() {
    // テキスト入力欄の確定で発火。入力を音声発話と同じ経路に流す。
    UIActionRegistry::Get().RegisterText("chat.send", [](const std::string& text) {
        if (text.empty()) return;
        SharedMediaContext& ctx = LaviContext::Get();
        GatekeeperManager* gk = ctx.gkManager; // GatekeeperSystem 公開（未初期化なら無視）
        if (!gk) return;
        // ユーザー発話として扱う（音声確定と同一フィールド）。記憶・ターン制御・GK も同値を参照する。
        ctx.transcribedText = text;
        gk->RespondToSpeech();
    });
}

void TextChatSystem::Finalize() {
    UIActionRegistry::Get().UnregisterText("chat.send");
}
