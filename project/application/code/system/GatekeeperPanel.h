#pragma once

#include <string>

struct SharedMediaContext;
class GatekeeperManager;

// 統合ゲートキーパーの単一パネル。
// 3 つの GK の設定 + 統合イベントフィード + ルーティング判断をまとめて表示する。
// 評価自体は GatekeeperManager::Update が UI と independ に毎フレーム行う。
class GatekeeperPanel {
public:
    void Initialize(SharedMediaContext* ctx);
    void Finalize();
    void SyncOnce(); // 既定パラメータの初回反映（毎フレーム無条件に呼ぶ。タブ非選択でも反映するため）
    void Draw();

private:
    void ApplyKeywords();
    void PushCameraParams();
    void PushScreenParams();

    SharedMediaContext* ctx_ = nullptr;
    GatekeeperManager* mgr_ = nullptr; // GatekeeperSystem 公開。Draw 入口で LaviContext から pull
    bool paramsPushed_ = false;        // 既定パラメータ反映を初回 Draw で一度だけ行う

    // --- Camera UI 状態 ---
    std::string ferModelPath_ = "application/resource/gatekeeper/emotion-ferplus-8.onnx";
    std::string haarPath_     = "application/resource/gatekeeper/haarcascade_frontalface_default.xml";
    bool camUseGpu_ = false;
    bool camLoaded_ = false;
    std::string camStatus_;
    float camThreshold_  = 0.5f;
    float scaleFactor_   = 1.1f;
    int minNeighbors_    = 5;
    int minFaceSize_     = 80;
    int confirmFrames_   = 1;
    bool ignoreNeutral_  = false;
    float neutralBias_   = 0.0f;

    // --- Screen UI 状態 ---
    bool watchFg_   = true;
    bool detectNew_ = true;
    bool useScreenDiff_ = true;
    float screenDiffThreshold_ = 0.05f;
    int pixelDiffThreshold_    = 25;

    // --- Mic UI 状態 ---
    std::string keywordText_ = "ねえ\nLAVI\n教えて";
    bool caseSensitive_ = false;

    // --- ホットキー / ウェイクワード UI 状態 ---
    char wakeWordBuf_[64] = {};
    int hotkeyModSel_ = 0; // 0=Ctrl+Shift, 1=Ctrl+Alt, 2=Alt+Shift
    char hotkeyKeyBuf_[4] = {};
};
