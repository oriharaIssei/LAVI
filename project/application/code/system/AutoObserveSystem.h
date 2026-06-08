#pragma once

#include "system/ISystem.h"

#include "LLMClient.h"
#include "VisionAnalyzer.h" // VisionResult（ローカル経路の戻り型）

#include <chrono>
#include <future>

/// <summary>
/// 自律観察（Auto-Observe）の本番 ECS システム。
/// GatekeeperConfig/動作中 Config の autoObserve* に従い、一定間隔でカメラ/画面フレームを
/// Claude（Vision）へ送り、自発的なコメントを生成する。応答は GatekeeperManager::SpeakProactive
/// 経由で会話メモリ記録＋発話（VoiceVox）へ流す（アクションは実行しない＝暴発防止）。
///
/// GK が応答中・LAVI 発話中・API キー未設定のときは観察を見送る。
/// 旧 LLMChatPanel の auto-observe（DEBUG 限定）の ECS 化。
/// </summary>
class AutoObserveSystem : public OriGine::ISystem {
public:
    AutoObserveSystem();
    ~AutoObserveSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    LLMClient llm_;                                  // クラウド Vision 呼び出し（自前所有）
    std::future<LLMResponse> future_;                // クラウド経路の結果
    std::future<VisionResult> localFuture_;          // ローカル経路（Qwen2.5-VL）の結果
    bool busy_ = false;                              // 観察リクエスト処理中か
    bool busyLocal_ = false;                         // 処理中リクエストがローカル経路か
    std::chrono::steady_clock::time_point lastTime_; // 直近の観察時刻
};
