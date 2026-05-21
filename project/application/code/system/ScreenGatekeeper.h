#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// 画面のゲートキーパー。
// ・WinAPI (ProcessManager) でフォアグラウンドアプリの切り替え / 新規ウィンドウを検出
// ・OpenCV で画面キャプチャの差分を検出 (画面遷移・通知ポップアップ等)
// いずれかの変化を Claude API へのエスカレーション候補とする。

struct ScreenGateResult {
    bool foregroundChanged = false;
    std::string foregroundApp;    // 現在のフォアグラウンド exe (UTF-8)
    std::string foregroundTitle;  // 現在のウィンドウタイトル (UTF-8)

    std::vector<std::string> newApps;  // 新規に出現した exe 名

    bool screenChanged = false;
    float screenDiffRatio = 0.0f;  // 変化画素の割合 (0..1)

    bool triggered = false;  // 上記いずれかが成立
};

class ScreenGatekeeper {
public:
    ScreenGatekeeper();
    ~ScreenGatekeeper();

    ScreenGatekeeper(const ScreenGatekeeper&) = delete;
    ScreenGatekeeper& operator=(const ScreenGatekeeper&) = delete;

    // プロセス監視 + (bgra があれば) 画面差分を評価。
    // bgra==nullptr の場合は画面差分をスキップする。
    ScreenGateResult Evaluate(const uint8_t* bgra, uint32_t width, uint32_t height);

    void SetScreenDiffThreshold(float ratio) { screenDiffThreshold_ = ratio; }  // 0..1
    void SetPixelDiffThreshold(int v) { pixelDiffThreshold_ = (v < 0) ? 0 : v; }
    void SetDetectNewProcesses(bool b) { detectNew_ = b; }
    void SetWatchForeground(bool b) { watchForeground_ = b; }

    void ResetState();

private:
    // プロセス監視の状態
    uint64_t lastForegroundKey_ = 0;  // HWND を uintptr 化したもの
    bool hasForeground_ = false;
    std::unordered_set<std::string> knownProcesses_;
    bool hasProcessBaseline_ = false;
    bool detectNew_ = true;
    bool watchForeground_ = true;

    // 画面差分の状態 (縮小グレースケールを保持)
    std::vector<uint8_t> prevGray_;
    int prevW_ = 0, prevH_ = 0;
    float screenDiffThreshold_ = 0.05f;  // 5%
    int pixelDiffThreshold_ = 25;
};
