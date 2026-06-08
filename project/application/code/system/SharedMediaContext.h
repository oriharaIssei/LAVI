#pragma once

#include <cstdint>
#include <future>
#include <string>
#include <vector>

#include "VoiceVoxClient.h"
#include "AppConfig.h"
#include "CameraGatekeeper.h" // EmotionResult を値で保持するため
#include "ScreenGatekeeper.h" // ScreenGateResult
#include "MicGatekeeper.h"    // MicGateResult

namespace OriGine {
class Microphone;
class WebCamera;
class ScreenCapture;
}

class LocationProvider;
class WebSearchClient;
class SentenceEmbedding;
class KnowledgeBase;
class ActionPipeline;
class MicrophonePanel;
class MemoryPanel;
class ConversationMemory;
class GatekeeperManager;
class TurnController;
class LongTermMemory;
class LocalLLM;
class VisionAnalyzer;
class LocalVisionAnalyzer;

struct SharedMediaContext {
    AppConfigData config;

    // ECS 分解で各 System が公開する共有サービス（非所有。所有は担当 System）。
    LocationProvider* location = nullptr; // LocationSystem が公開する現在地プロバイダ
    WebSearchClient* webSearch = nullptr; // WebSearchSystem が公開する時事Web検索
    SentenceEmbedding* embedding = nullptr; // KnowledgeSystem が公開する文埋め込み
    KnowledgeBase* knowledgeBase = nullptr; // KnowledgeSystem が公開する知識ベース RAG
    ActionPipeline* actionPipeline = nullptr; // ActionSystem が公開する Intent-Driven アクション実行
    MicrophonePanel* micPanel = nullptr;      // MicrophoneSystem 所有。TurnSystem が参照
    MemoryPanel* memoryPanel = nullptr;       // MemorySystem 所有。DEBUG UI(LLMChatPanel/monolith Draw) が参照
    GatekeeperManager* gkManager = nullptr;   // GatekeeperSystem 所有（会話エンジン）。各所が参照
    TurnController* turnController = nullptr; // TurnSystem が公開する発話区間検出・ターン制御
    VisionAnalyzer* visionAnalyzer = nullptr; // VisionSystem が公開する画像解析（Claude Vision）
    LocalVisionAnalyzer* localVisionAnalyzer = nullptr; // VisionSystem が公開するローカル画像解析（Qwen2.5-VL）

    // キャプチャ評価系（Camera/Screen/MicGateSystem が所有・公開）。検知結果を CapturePromptComponent
    // として発行し、GatekeeperSystem が消費する。GK ポインタは GatekeeperPanel のチューニング用。
    CameraGatekeeper* cameraGate = nullptr;
    ScreenGatekeeper* screenGate = nullptr;
    MicGatekeeper* micGate = nullptr;
    EmotionResult camResult;       // CameraGateSystem が毎評価で更新
    ScreenGateResult screenResult; // ScreenGateSystem が毎評価で更新
    MicGateResult micResult;       // MicGateSystem が毎評価で更新
    uint32_t camFrameW = 0, camFrameH = 0; // 直近のカメラフレーム寸法（顔識別用）
    LongTermMemory* longTermMemory = nullptr; // MemoryPanel 所有。GatekeeperSystem 等が参照
    LocalLLM* localLLM = nullptr;             // MemoryPanel 所有。GatekeeperSystem 等が参照
    ConversationMemory* conversationMemory = nullptr; // MemoryPanel 所有。チャット履歴表示・assistant 記録用

    OriGine::WebCamera* webCamera = nullptr;
    OriGine::ScreenCapture* screenCapture = nullptr; // 主モニタ（screen-diff 用・先頭）
    std::vector<OriGine::ScreenCapture*> screenCaptures; // 撮影中の全モニタ（ScreenCaptureSystem 所有）
    std::vector<uint8_t> camFrameBuffer;
    std::vector<uint8_t> screenFrameBuffer;

    VoiceVoxClient* voiceVox = nullptr;
    std::vector<VoiceVoxSpeaker> voiceVoxSpeakers;
    int selectedSpeaker = 0;
    bool isSpeaking = false;
    std::future<bool> speakFuture;

    std::string transcribedText;

    // 声紋登録用: MicrophonePanel がコピーを書き込む
    std::vector<float> voiceSnapshotBuffer;
    uint32_t voiceSnapshotSampleRate = 16000;
    bool voiceSnapshotReady = false;

    // 顔識別結果 (MemoryPanel が毎フレーム更新)
    std::string identifiedUserName;
    float identifiedSimilarity = 0.0f;
    bool userIdentified = false;

    // ホットキー / ウェイクワード
    bool hotkeyEnabled = true;
    int hotkeyModifiers = 0x0006; // MOD_CONTROL | MOD_SHIFT
    int hotkeyVk = 'L';
    std::string wakeWord = "LAVI";
    bool wakeWordEnabled = true;
};
