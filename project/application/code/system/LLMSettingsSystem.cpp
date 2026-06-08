#include "LLMSettingsSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "system/ui/UIRegistry.h"
#include "system/component/ui/UITextInputComponent.h"

#include "component/ComponentArray.h"

#include <string>

namespace {
constexpr const char* kApiKeyAction = "llm.apikey.set";

// 前後の空白・改行を取り除く（貼り付け時の余分な文字対策）。
std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}
} // namespace

LLMSettingsSystem::LLMSettingsSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

LLMSettingsSystem::~LLMSettingsSystem() = default;

void LLMSettingsSystem::Initialize() {
    // API キー入力欄の確定 → config 更新＋永続化。空入力は誤確定による消去を避けるため無視。
    UIActionRegistry::Get().RegisterText(kApiKeyAction, [](const std::string& text) {
        const std::string key = Trim(text);
        if (key.empty()) return;
        SharedMediaContext& ctx = LaviContext::Get();
        ctx.config.apiKey = key;
        SaveAppConfig(ctx.config);
    });
}

void LLMSettingsSystem::Finalize() {
    UIActionRegistry::Get().UnregisterText(kApiKeyAction);
}

void LLMSettingsSystem::Update() {
    auto* arr = GetComponentArray<UITextInputComponent>();
    if (!arr) {
        return;
    }
    const bool hasKey = !LaviContext::Get().config.apiKey.empty();
    for (auto& slot : arr->GetSlotsRef()) {
        for (auto& ti : slot.components) {
            if (ti.actionId != kApiKeyAction || ti.focused) {
                continue; // 入力中は触らない
            }
            ti.placeholder = hasKey ? "設定済み（再入力で上書き）" : "API キーを入力して Enter";
        }
    }
}
