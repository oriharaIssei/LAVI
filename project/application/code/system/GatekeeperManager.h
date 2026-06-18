#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CameraGatekeeper.h"
#include "ScreenGatekeeper.h"
#include "MicGatekeeper.h"
#include "LLMClient.h"
#include "LocalLLM.h"
#include "WebAction.h" // ResolvedWebAction（observe ワーカーへ先解決済みブラウザ操作を渡す）

#include <future>

struct SharedMediaContext;
class LongTermMemory;
class KnowledgeBase;
class WebSearchClient;
class ActionPipeline;

enum class GateSource { Camera, Screen, Mic };

// ReAct ループの実行フェーズ。LLM 生成中とツール実行(observe)中を相互排他で表し、
// いずれの非 Idle 状態でも新しい発話ターンを弾く（直列化をツール実行にも及ぼす）。
enum class ReactPhase { Idle, LlmBusy, ToolBusy };

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
        bool useWebContext = true;     // 時事クエリ検出時に Web 検索結果を文脈注入する（時事対策）

        // 自律観察（AutoObserveSystem）: 一定間隔でカメラ/画面を見て自発的にコメントする。
        bool autoObserveEnabled  = false;
        bool autoObserveWebCam   = true;
        bool autoObserveScreen   = false;
        float autoObserveInterval = 10.0f; // 観察間隔 (秒)

        bool visionUseLocal = false; // 画像処理をローカル VLM(Qwen2.5-VL) で行う（false=クラウド）

        int maxReactIterations = 4; // ツール実行→観測→再思考の最大反復回数
    };

    GatekeeperManager();
    ~GatekeeperManager();

    GatekeeperManager(const GatekeeperManager&) = delete;
    GatekeeperManager& operator=(const GatekeeperManager&) = delete;

    void Initialize(SharedMediaContext* ctx);
    // 共有依存はすべて各 System が LaviContext に公開し、判定処理の入口で遅延参照する（Set 廃止）：
    //   LongTermMemory/LocalLLM=MemoryPanel, 埋め込み/知識ベース=KnowledgeSystem,
    //   時事Web検索=WebSearchSystem, アクション実行=ActionSystem。
    void Update();  // MediaCaptureDemoSystem::Update から毎フレーム呼ぶ

    // GK 本体・結果・フレーム寸法は Camera/Screen/MicGateSystem が所有し LaviContext に公開する。
    // 互換のためアクセサは維持し、中身は ctx を参照する（GatekeeperPanel/MemoryPanel は無改修）。
    CameraGatekeeper* Camera();
    ScreenGatekeeper* Screen();
    MicGatekeeper* Mic();

    const EmotionResult& CameraResult() const;
    const ScreenGateResult& ScreenResult() const;
    const MicGateResult& MicResult() const;

    uint32_t CameraFrameWidth() const;
    uint32_t CameraFrameHeight() const;

    // キャプチャ System が発行した CapturePromptComponent を 1 件取り込む（GatekeeperSystem が駆動）。
    void IngestPrompt(GateSource src, std::string desc);

    Config& config() { return config_; }
    const std::vector<GateEvent>& Events() const { return events_; }
    const std::vector<RouteDecision>& Decisions() const { return decisions_; }

    void ClearLog();

    // 直近の判断を手動で Claude へ送出する
    void EscalateLatest();

    // 発話区間検出で確定したユーザー発話(ctx_->transcribedText)を
    // 会話としてゲートキーパー経由で LLM へ送出する（自動ターン応答用）。
    void RespondToSpeech();

    // 自律観察など外部 System が生成した LAVI 発話を、会話メモリ記録＋発話（VoiceVox）に流す。
    // 安全のためアクション（[action:]/[browser:]）は実行しない（自発観察での暴発防止）。
    void SpeakProactive(const std::string& content);

    // LLM 生成中・ツール実行中のいずれも busy として返す（ターン直列化の単一判定）。
    bool LlmBusy() const { return phase_ != ReactPhase::Idle; }
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
    std::string BuildReactSystemPrompt(const std::string& basePrompt) const;
    std::string BuildRecentDecisionContext() const;
    // observe ワーカー本体。ブラウザ操作は LTM 競合回避のためメインで先解決済みのものを受け取る。
    std::string ExecuteTaggedToolsAndBuildObservation(const std::string& content,
                                                      const std::vector<ResolvedWebAction>& resolvedBrowser);
    bool ContinueReactLoop(const std::string& content, const std::string& observation);
    void FinalizeLlmResponse(const std::string& content);
    void ResetReactState();

    // ツール実行タグ([search:]/[action:]/[browser:])が含まれるかの軽量判定（メインスレッドで実行）。
    bool HasToolTags(const std::string& content) const;
    // LLM 応答を受けて、ツール実行(observe)をワーカーで起動するか、最終確定するかを振り分ける。
    void BeginObserveOrFinalize(const std::string& content);

    SharedMediaContext* ctx_ = nullptr;

    // GK 本体・評価結果・評価間隔の状態は Camera/Screen/MicGateSystem へ移譲（ECS 分解）。
    Config config_;

    std::vector<GateEvent> events_;    // 直近の出来事 (上限あり)
    std::vector<GateEvent> pending_;   // 合成待ち
    double pendingStart_ = 0.0;
    std::vector<RouteDecision> decisions_;  // 直近の判断 (上限あり)

    // LLM 送出 (エスカレーション)
    LLMClient llm_;                          // クラウド (Claude)
    std::future<LLMResponse> llmFuture_;
    LocalLLM* localLLM_ = nullptr;           // ローカル (共有インスタンス。所有しない)
    std::future<std::string> localFuture_;
    std::future<std::string> observeFuture_; // ツール実行(observe)をワーカーで回す。完了は ToolBusy で回収
    ReactPhase phase_ = ReactPhase::Idle; // LLM 生成中 / ツール実行中 を表す状態機械
    std::string lastResponse_;
    double lastEscalate_ = -1.0e9;
    bool reactActive_ = false;
    bool reactUseLocal_ = false;
    bool reactForceFinal_ = false;
    int reactIteration_ = 0;
    std::string reactSystemPrompt_;
    std::vector<LocalChatMessage> reactMessages_;
    std::string reactLastContent_; // observe 起動時の LLM 応答（ToolBusy 完了時に ContinueReactLoop へ渡す）
    LongTermMemory* longTermMemory_ = nullptr;
    KnowledgeBase* knowledgeBase_ = nullptr;  // 知識ベース RAG（共有。KnowledgeSystem 公開。入口で LaviContext から引く）
    WebSearchClient* webSearch_ = nullptr;    // Web 検索（時事対策。共有インスタンス。所有しない）
    ActionPipeline* actionPipeline_ = nullptr; // 共有。ActionSystem 公開。入口で LaviContext から引く
};
