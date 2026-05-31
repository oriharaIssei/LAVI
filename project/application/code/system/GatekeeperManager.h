#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CameraGatekeeper.h"
#include "ScreenGatekeeper.h"
#include "MicGatekeeper.h"
#include "LLMClient.h"

#include <future>

struct SharedMediaContext;
class LongTermMemory;
class SentenceEmbedding;
class ActionPipeline;
class LocalLLM;

enum class GateSource { Camera, Screen, Mic };

// 各 GK が発火した 1 件の出来事
struct GateEvent {
    GateSource source;
    std::string description;  // 人間可読 (Claude へのコンテキストにも使う)
    double time = 0.0;
};

// 意図分類によるルーティング先
enum class RouteTarget {
    None,
    Conversation,  // マイクのキーワード等 → 会話
    WebSearch,     // 「調べて」等 → Web検索付き会話
    VisionScreen,  // 画面変化 → 画面を見て反応
    VisionCamera,  // 表情変化 → ユーザーを気遣う
    Multi          // 複数ソースの組み合わせ
};

// 集約後の 1 件のルーティング判断 (実行計画)
struct RouteDecision {
    RouteTarget target = RouteTarget::None;
    std::string prompt;                  // Claude へ渡す生成済みコンテキスト
    std::vector<GateSource> sources;
    double time = 0.0;
};

// 3 つのゲートキーパーを所有し、UI のタブ選択に関係なく毎フレーム評価して
// トリガーを集約・意図分類・ルーティング判断する統合層。
class GatekeeperManager {
public:
    struct Config {
        bool camEnabled = false;  // カメラはモデル読み込み後に有効化
        bool screenEnabled = true;
        bool micEnabled = true;

        float camInterval = 0.3f;     // 各 GK の評価間隔 (秒)
        float screenInterval = 0.5f;
        float micInterval = 0.2f;

        float combineWindow = 1.5f;   // この時間内のトリガーを 1 件に合成

        bool autoEscalate = false;     // 判断を自動で Claude へ送出するか
        float escalateCooldown = 8.0f; // 自動送出の最小間隔 (秒)
        bool autoSpeak = true;         // 応答を VoiceVox で発話するか
        bool useLocalLLM = true;       // 応答生成にローカル LLM を使う（未ロード時はクラウドへフォールバック）
    };

    GatekeeperManager();
    ~GatekeeperManager();

    GatekeeperManager(const GatekeeperManager&) = delete;
    GatekeeperManager& operator=(const GatekeeperManager&) = delete;

    void Initialize(SharedMediaContext* ctx);
    void SetLongTermMemory(LongTermMemory* mem) { longTermMemory_ = mem; }
    void SetSentenceEmbedding(SentenceEmbedding* emb) { embedding_ = emb; }
    void SetActionPipeline(ActionPipeline* pipeline) { actionPipeline_ = pipeline; }
    void SetLocalLLM(LocalLLM* llm) { localLLM_ = llm; }
    void Update();  // MediaCaptureDemoSystem::Update から毎フレーム呼ぶ

    CameraGatekeeper* Camera() { return camera_.get(); }
    ScreenGatekeeper* Screen() { return screen_.get(); }
    MicGatekeeper* Mic() { return mic_.get(); }

    const EmotionResult& CameraResult() const { return camResult_; }
    const ScreenGateResult& ScreenResult() const { return screenResult_; }
    const MicGateResult& MicResult() const { return micResult_; }

    uint32_t CameraFrameWidth() const { return lastCamW_; }
    uint32_t CameraFrameHeight() const { return lastCamH_; }

    Config& config() { return config_; }
    const std::vector<GateEvent>& Events() const { return events_; }
    const std::vector<RouteDecision>& Decisions() const { return decisions_; }

    void ClearLog();

    // 直近の判断を手動で Claude へ送出する
    void EscalateLatest();

    // 発話区間検出で確定したユーザー発話(ctx_->transcribedText)を
    // 会話としてゲートキーパー経由で LLM へ送出する（自動ターン応答用）。
    void RespondToSpeech();
    bool LlmBusy() const { return llmBusy_; }
    const std::string& LastResponse() const { return lastResponse_; }

    static const char* SourceName(GateSource s);
    static const char* TargetName(RouteTarget t);

private:
    double NowSec() const;
    void PushEvent(GateSource src, std::string desc, double now);
    void FlushPending(double now);
    RouteTarget Classify(const std::vector<GateEvent>& evs) const;
    static std::string BuildPrompt(const std::vector<GateEvent>& evs, RouteTarget t);

    void Dispatch(const RouteDecision& dec, double now);
    void PollLlm();
    void Speak(const std::string& text);

    SharedMediaContext* ctx_ = nullptr;

    std::unique_ptr<CameraGatekeeper> camera_;
    std::unique_ptr<ScreenGatekeeper> screen_;
    std::unique_ptr<MicGatekeeper> mic_;

    Config config_;

    double lastCam_ = 0.0, lastScreen_ = 0.0, lastMic_ = 0.0;

    EmotionResult camResult_;
    ScreenGateResult screenResult_;
    MicGateResult micResult_;

    std::vector<GateEvent> events_;    // 直近の出来事 (上限あり)
    std::vector<GateEvent> pending_;   // 合成待ち
    double pendingStart_ = 0.0;
    std::vector<RouteDecision> decisions_;  // 直近の判断 (上限あり)

    uint32_t lastCamW_ = 0, lastCamH_ = 0;
    uint32_t lastScreenW_ = 0, lastScreenH_ = 0;

    // LLM 送出 (エスカレーション)
    LLMClient llm_;                          // クラウド (Claude)
    std::future<LLMResponse> llmFuture_;
    LocalLLM* localLLM_ = nullptr;           // ローカル (共有インスタンス。所有しない)
    std::future<std::string> localFuture_;
    bool llmBusy_ = false;
    std::string lastResponse_;
    double lastEscalate_ = -1.0e9;
    LongTermMemory* longTermMemory_ = nullptr;
    SentenceEmbedding* embedding_ = nullptr;
    ActionPipeline* actionPipeline_ = nullptr;
};
