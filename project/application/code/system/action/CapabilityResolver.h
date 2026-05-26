#pragma once

#include "ActionTypes.h"

class CapabilityResolver {
public:
    CapabilityResult Resolve(const IntentResult& intent) const;
};
