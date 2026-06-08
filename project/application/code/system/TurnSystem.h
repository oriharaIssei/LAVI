#pragma once

#include "system/ISystem.h"

#include <chrono>
#include <memory>
#include <string>

class TurnController;
class MicrophonePanel;
class GatekeeperManager;

/// <summary>
/// 発話区間検出・ターン制御を担う ECS システム（Category=StateTransition）。
/// TurnController を所有し、毎フレーム マイクレベル/発話状態を駆動して
/// 「ユーザー発話 → 無音で自動転写 → GateKeeper 経由で LLM 自動応答」のターンを回す。
/// barge-in（LAVI 発話中の割り込み）で TTS を停止する。
///
/// 依存（micPanel/gkManager）はモノリス所有のものを LaviContext から参照。
/// TurnController は本 System が所有し LaviContext に公開（UI が参照）。
/// MediaCaptureDemoSystem::Update からの切り出し（ECS 分解）。パターンは KnowledgeSystem と同型。
/// </summary>
class TurnSystem : public OriGine::ISystem {
public:
    TurnSystem();
    ~TurnSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    std::unique_ptr<TurnController> turnController_;

    // 消費（非所有）。コールバックが this 経由で見るためメンバ保持し、各 Update 入口で pull。
    MicrophonePanel* micPanel_ = nullptr;
    GatekeeperManager* gkManager_ = nullptr;

    // ターン制御の実行時状態（モノリスから移管）。
    std::chrono::steady_clock::time_point lastTurnTick_;
    bool prevSpeaking_ = false;
    bool processingActive_ = false;
    std::chrono::steady_clock::time_point processingSince_;
    std::string lastTurnTranscript_;
};
