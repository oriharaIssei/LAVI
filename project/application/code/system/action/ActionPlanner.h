#pragma once

#include "ActionTypes.h"

#include <string>
#include <vector>

class ActionPlanner {
public:
    std::vector<ActionCommand> Plan(const IntentResult& intent,
                                    const CapabilityResult& capability,
                                    const UserPreference& preference,
                                    const std::string& serviceName,
                                    const std::vector<std::string>& keywords) const;
};
