#pragma once

#include "ActionTypes.h"
#include "IntentExtractor.h"
#include "CapabilityResolver.h"
#include "MemoryResolver.h"
#include "ActionPlanner.h"
#include "ActionQueue.h"
#include "ActionExecutor.h"

class LongTermMemory;
class SentenceEmbedding;

class ActionPipeline {
public:
    bool LoadConfig(const std::string& tagAxesPath);

    ActionResult Process(const std::string& speech,
                         LongTermMemory* memory,
                         SentenceEmbedding* embedding = nullptr);

    bool ContainsActionKeyword(const std::string& text) const;

    const IntentExtractor& GetIntentExtractor() const { return extractor_; }

    // TODO: expose queue for GUI Agent multi-step actions

private:
    IntentExtractor extractor_;
    CapabilityResolver capabilityResolver_;
    MemoryResolver memoryResolver_;
    ActionPlanner planner_;
    ActionExecutor executor_;
    ActionQueue queue_;
};
