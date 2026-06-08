#pragma once

#include "system/ISystem.h"

#include <memory>

class WebSearchClient;

/// <summary>
/// 時事 Web 検索（WebSearchClient）を担う ECS システム。
/// クライアントを所有し、初期化時に時事キーワード(JSON)を読み込んで LaviContext に公開する。
/// 消費側（LLMChatPanel / GatekeeperManager）は LaviContext::Get().webSearch を参照する。
///
/// MediaCaptureDemoSystem からの切り出し（ECS 分解）。パターンは LocationSystem と同じ。
/// </summary>
class WebSearchSystem : public OriGine::ISystem {
public:
    WebSearchSystem();
    ~WebSearchSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override {} // 検索は要求時に実行。毎フレーム処理は不要。

private:
    std::unique_ptr<WebSearchClient> webSearch_;
};
