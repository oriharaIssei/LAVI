#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CameraGatekeeper.h"
#include "ScreenGatekeeper.h"
#include "MicGatekeeper.h"

struct SharedMediaContext;

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
    };

    GatekeeperManager();
    ~GatekeeperManager();

    GatekeeperManager(const GatekeeperManager&) = delete;
    GatekeeperManager& operator=(const GatekeeperManager&) = delete;

    void Initialize(SharedMediaContext* ctx);
    void Update();  // MediaCaptureDemoSystem::Update から毎フレーム呼ぶ

    CameraGatekeeper* Camera() { return camera_.get(); }
    ScreenGatekeeper* Screen() { return screen_.get(); }
    MicGatekeeper* Mic() { return mic_.get(); }

    const EmotionResult& CameraResult() const { return camResult_; }
    const ScreenGateResult& ScreenResult() const { return screenResult_; }
    const MicGateResult& MicResult() const { return micResult_; }

    Config& config() { return config_; }
    const std::vector<GateEvent>& Events() const { return events_; }
    const std::vector<RouteDecision>& Decisions() const { return decisions_; }

    void ClearLog();

    static const char* SourceName(GateSource s);
    static const char* TargetName(RouteTarget t);

private:
    double NowSec() const;
    void PushEvent(GateSource src, std::string desc, double now);
    void FlushPending(double now);
    static RouteTarget Classify(const std::vector<GateEvent>& evs);
    static std::string BuildPrompt(const std::vector<GateEvent>& evs, RouteTarget t);

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
};
