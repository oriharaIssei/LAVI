#pragma once

#include "ActionTypes.h"

#include <string>

class ActionExecutor {
public:
    ActionResult Execute(const ActionCommand& cmd);

private:
    void OpenUrl(const std::string& url, const std::string& browserProcess);
    bool LaunchApplication(const std::string& appName);
    void AutoClickFirstResult(int delayMs);
};
