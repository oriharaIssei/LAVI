#include "WebSearchSystem.h"

#include "WebSearchClient.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"

WebSearchSystem::WebSearchSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

WebSearchSystem::~WebSearchSystem() = default;

void WebSearchSystem::Initialize() {
    webSearch_ = std::make_unique<WebSearchClient>();
    // 時事キーワードは JSON から読み込む（無ければ既定値）。
    webSearch_->LoadKeywords("application/resource/web/current_events.json");
    LaviContext::Get().webSearch = webSearch_.get(); // 共有状態に公開（消費側は LaviContext 経由）
}

void WebSearchSystem::Finalize() {
    LaviContext::Get().webSearch = nullptr;
    webSearch_.reset();
}
