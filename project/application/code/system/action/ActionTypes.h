#pragma once

#include <string>

// 行動パイプラインの結果（実行後の表示・発話用）。
// Intent/Capability/ActionType 等の enum は廃止し、ActionRequest.h + ToolRegistry へ移行した。
struct ActionResult {
    bool handled = false;
    std::string url;
    std::string serviceName;
    std::string query;
    std::string spokenText;
};
