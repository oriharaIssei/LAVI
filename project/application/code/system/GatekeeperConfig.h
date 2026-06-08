#pragma once

#include <string>
#include <vector>

/// <summary>
/// ゲートキーパー(カメラ/画面/マイク)の調整パラメータ。GlobalVariables(Scene="Settings",
/// Group="Gatekeeper") に永続化し、各 GateSystem が Initialize で読み込んで GK へ適用する。
///
/// 目的: 従来 GatekeeperPanel(DEBUG 限定)にハードコードされ「Load Models / SyncOnce」ボタン経由でしか
/// GK に反映されなかったパラメータ・モデルパスを JSON 設定化し、Release でも GK が動くようにする
/// （mem:feedback_no_hardcode / mem:project_ecs_decomposition）。
/// 既定値は従来パネルの初期値に一致させ、設定ファイル不在時も挙動を保つ。
/// </summary>
struct GatekeeperConfigData {
    // --- 統合フラグ / 評価間隔（GatekeeperManager::Config に対応）---
    bool camEnabled    = false; // カメラはモデル読み込み＋明示有効化が必要（既定 OFF＝プライバシー安全側）
    bool screenEnabled = true;
    bool micEnabled    = true;
    float camInterval    = 0.3f;
    float screenInterval = 0.5f;
    float micInterval    = 0.2f;
    float combineWindow  = 1.5f;
    bool autoEscalate    = false;
    float escalateCooldown = 8.0f;
    bool autoSpeak    = true;
    bool useLocalLLM  = true;
    bool useWebContext = true;

    // --- 自律観察（AutoObserveSystem）---
    bool autoObserveEnabled  = false; // 一定間隔でカメラ/画面を見て自発コメント
    bool autoObserveWebCam   = true;
    bool autoObserveScreen   = false;
    float autoObserveInterval = 10.0f; // 観察間隔 (秒)

    // --- カメラ GK (FER+) ---
    std::string ferModelPath = "application/resource/gatekeeper/emotion-ferplus-8.onnx";
    std::string haarPath     = "application/resource/gatekeeper/haarcascade_frontalface_default.xml";
    int camDeviceIndex = -1;    // 使用カメラ番号（-1=既定デバイス）
    bool camUseGpu     = false;
    float camThreshold = 0.5f;
    float scaleFactor  = 1.1f;
    int minNeighbors   = 5;
    int minFaceSize    = 80;
    int confirmFrames  = 1;
    bool ignoreNeutral = false;
    float neutralBias  = 0.0f;

    // --- 画面 GK ---
    bool watchFg   = true;
    bool detectNew = true;
    bool useScreenDiff = true;
    float screenDiffThreshold = 0.05f;
    int pixelDiffThreshold    = 25;

    // --- マイク GK ---
    std::string keywordsText = "ねえ\nLAVI\n教えて"; // 1 行 1 語
    bool caseSensitive = false;
};

/// <summary>設定の読み込み（不在キーは既定値で補完）。</summary>
GatekeeperConfigData LoadGatekeeperConfig();
/// <summary>設定の保存。</summary>
void SaveGatekeeperConfig(const GatekeeperConfigData& config);

/// <summary>改行/復帰区切りのキーワード文字列を語のリストへ分割（空行は除外）。</summary>
std::vector<std::string> SplitGatekeeperKeywords(const std::string& text);
