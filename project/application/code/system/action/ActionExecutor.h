#pragma once

#include "ActionTypes.h"
#include "ActionRequest.h"
#include "ToolRegistry.h"
#include "ActionPlanner.h"

#include <string>

/// <summary>
/// ActionRequest を実際の OS 操作（ブラウザ起動 / アプリ起動）へ落とし込む実行器（手）。
/// 対象の解決・計画は ToolRegistry / ActionPlanner が行い、ここは計画に従って実行するだけ。
/// </summary>
class ActionExecutor {
public:
    // 実行計画を順に実行する。最後に成功したステップの結果を返す。
    ActionResult ExecutePlan(const ActionPlan& plan);

    // 解決済みの Tool に対して単一の request を実行する。
    ActionResult Execute(const ActionRequest& request, const Tool& tool);

private:
    void OpenUrl(const std::string& url, const std::string& browserProcess = "");
    bool LaunchApplication(const std::string& appName);
    void AutoClickFirstResult(int delayMs);
};
