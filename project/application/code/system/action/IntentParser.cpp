#include "IntentParser.h"

#include <nlohmann/json.hpp>

namespace {

constexpr char kTag[] = "[action:";

// open 位置にある '{' から対応する '}' を見つけ、その JSON オブジェクト文字列を返す。
// 見つからなければ空。endOut には '}' の次位置（or npos）を入れる。
std::string ExtractJsonObject(const std::string& s, size_t from, size_t& endOut) {
    const size_t brace = s.find('{', from);
    if (brace == std::string::npos) { endOut = std::string::npos; return {}; }
    int depth = 0;
    bool inStr = false;
    bool esc = false;
    for (size_t i = brace; i < s.size(); ++i) {
        const char c = s[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}') {
                if (--depth == 0) { endOut = i + 1; return s.substr(brace, i - brace + 1); }
            }
        }
    }
    endOut = std::string::npos;
    return {};
}

} // namespace

namespace ActionIntent {

std::vector<ActionRequest> Parse(const std::string& llmOutput) {
    std::vector<ActionRequest> out;
    size_t pos = 0;
    while ((pos = llmOutput.find(kTag, pos)) != std::string::npos) {
        size_t jsonEnd = std::string::npos;
        const std::string obj = ExtractJsonObject(llmOutput, pos + sizeof(kTag) - 1, jsonEnd);
        if (obj.empty() || jsonEnd == std::string::npos) { pos += sizeof(kTag) - 1; continue; }
        try {
            const nlohmann::json j = nlohmann::json::parse(obj);
            ActionRequest req;
            req.verb   = j.value("verb", std::string());
            req.target = j.value("target", std::string());
            req.query  = j.value("query", std::string());
            if (req.IsValid()) out.push_back(std::move(req));
        } catch (...) {
            // 不正な JSON は無視
        }
        pos = jsonEnd;
    }
    return out;
}

std::string Strip(const std::string& llmOutput) {
    std::string out;
    size_t i = 0;
    while (i < llmOutput.size()) {
        const size_t tag = llmOutput.find(kTag, i);
        if (tag == std::string::npos) { out += llmOutput.substr(i); break; }
        out += llmOutput.substr(i, tag - i);

        size_t jsonEnd = std::string::npos;
        ExtractJsonObject(llmOutput, tag + sizeof(kTag) - 1, jsonEnd);
        if (jsonEnd == std::string::npos) { i = tag + sizeof(kTag) - 1; continue; }
        // '}' の後に続く ']' まで（空白を許容）読み飛ばす
        size_t k = jsonEnd;
        while (k < llmOutput.size() && (llmOutput[k] == ' ' || llmOutput[k] == '\t')) ++k;
        if (k < llmOutput.size() && llmOutput[k] == ']') ++k;
        i = k;
    }
    // 余分な空白・改行を整理
    size_t b = out.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = out.find_last_not_of(" \t\r\n");
    return out.substr(b, e - b + 1);
}

} // namespace ActionIntent
