#include "ActionPlanner.h"

#include <cctype>
#include <sstream>

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string NormalizeVerb(const std::string& raw) {
    const std::string v = Lower(Trim(raw));
    if (v == "play" || v == "watch" || v == "listen") return "search";
    if (v == "run" || v == "start" || v == "launch")  return "launch";
    if (v == "open" || v == "go")                      return "open";
    if (v == "search" || v == "find")                  return "search";
    return v.empty() ? "open" : v;
}

bool SameRequest(const ActionRequest& a, const ActionRequest& b) {
    return a.verb == b.verb && a.target == b.target && a.query == b.query;
}

} // namespace

ActionPlan ActionPlanner::Build(const std::vector<ActionRequest>& requests,
                                const ToolRegistry& registry) const {
    ActionPlan plan;
    const Tool* fallback = registry.Find("google");

    for (const auto& raw : requests) {
        if (static_cast<int>(plan.steps.size()) >= maxSteps) break;

        ActionRequest req;
        req.verb   = NormalizeVerb(raw.verb);
        req.target = Trim(raw.target);
        req.query  = Trim(raw.query);
        if (!req.IsValid()) continue;

        const Tool* tool = registry.Find(req.target);
        if (!tool) {
            // 未知ツール → Google 検索フォールバック（target+query を検索語に）
            if (!fallback) continue;
            std::string q = req.target;
            if (!req.query.empty()) { if (!q.empty()) q += " "; q += req.query; }
            req.verb  = "search";
            req.query = q;
            req.target = fallback->name;
            tool = fallback;
        }

        // 連続する同一要求は除外（同じタブを二重に開かない）
        if (!plan.steps.empty() && SameRequest(plan.steps.back().request, req) &&
            plan.steps.back().tool == tool) {
            continue;
        }

        ActionStep step;
        step.request = std::move(req);
        step.tool = tool;
        plan.steps.push_back(std::move(step));
    }

    return plan;
}

std::string ActionPlan::Describe() const {
    std::ostringstream os;
    for (size_t i = 0; i < steps.size(); ++i) {
        const ActionStep& s = steps[i];
        if (i) os << "\xe3\x80\x81"; // 、
        const std::string& name = s.tool ? s.tool->name : s.request.target;
        if (s.tool && s.tool->IsApp()) {
            os << name << "\xe3\x82\x92\xe8\xb5\xb7\xe5\x8b\x95"; // を起動
        } else if (s.request.verb == "search" || !s.request.query.empty()) {
            os << name << "\xe3\x81\xa7\xe3\x80\x8c" << s.request.query
               << "\xe3\x80\x8d\xe3\x82\x92\xe6\xa4\x9c\xe7\xb4\xa2"; // で「query」を検索
        } else {
            os << name << "\xe3\x82\x92\xe9\x96\x8b\xe3\x81\x8f"; // を開く
        }
    }
    return os.str();
}
