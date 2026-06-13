#pragma once

#include <string>

struct AppConfigData {
    std::string apiKey;
    std::string llmSystemPrompt;
    std::string visionPrompt;
    bool minimizeToTrayOnClose = false; // 閉じるボタン(X)でトレイに最小化するか（false=X で終了）

    // 画像を「動画的に解釈」する設定：直近フレームを時系列で複数枚まとめて Vision に渡す。
    bool visionVideoEnabled    = true; // false=従来の単一フレーム送信
    int visionVideoFrames      = 3;    // 1 回に送る時系列フレーム数（ソース毎）
    float visionVideoWindowSec = 1.5f; // サンプリングする時間窓（秒）

    static const char* DefaultLLMSystemPrompt();
    static const char* DefaultVisionPrompt();
};

AppConfigData LoadAppConfig();
void SaveAppConfig(const AppConfigData& config);
