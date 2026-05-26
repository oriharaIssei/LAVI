#include "ActionPipeline.h"
#include "LongTermMemory.h"

bool ActionPipeline::LoadConfig(const std::string& tagAxesPath) {
    return extractor_.LoadConfig(tagAxesPath);
}

ActionResult ActionPipeline::Process(const std::string& speech,
                                     LongTermMemory* memory,
                                     SentenceEmbedding* /*embedding*/) {
    ActionResult finalResult;

    // 1. Intent Extraction
    IntentResult intent = extractor_.Extract(speech);
    if (intent.type == IntentType::None) return finalResult;

    // 2. Capability Resolution
    CapabilityResult capability = capabilityResolver_.Resolve(intent);

    // 3. Memory / Context Resolve
    UserPreference preference = memoryResolver_.ResolvePreferences(memory);
    std::string serviceName = memoryResolver_.ResolveService(
        capability, intent, preference, memory);
    std::vector<std::string> keywords = memoryResolver_.ResolveQueryKeywords(
        intent,
        extractor_.GetInterestCategories(),
        extractor_.GetSynonyms(),
        memory);

    // 4. Action Planning
    auto commands = planner_.Plan(intent, capability, preference, serviceName, keywords);

    // 5. Enqueue
    queue_.Clear();
    for (auto& cmd : commands) {
        queue_.Enqueue(std::move(cmd));
    }

    // 6. Execute
    while (queue_.HasPending()) {
        auto cmd = queue_.Dequeue();
        ActionResult stepResult = executor_.Execute(cmd);
        if (stepResult.handled) {
            finalResult = stepResult;
            if (!serviceName.empty()) {
                finalResult.serviceName = serviceName;
            }
            finalResult.query = cmd.label;
            const std::string label = cmd.label.empty() ? serviceName : cmd.label;
            finalResult.spokenText = label +
                "\xE3\x82\x92\xE9\x96\x8B\xE3\x81\x8F\xE3\x81\xAD"; // を開くね
        }
    }

    return finalResult;
}

bool ActionPipeline::ContainsActionKeyword(const std::string& text) const {
    return extractor_.ContainsActionKeyword(text);
}
