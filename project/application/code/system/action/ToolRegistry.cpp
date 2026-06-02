#include "ToolRegistry.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

std::string ToolRegistry::ToLower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string ToolRegistry::UrlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    char buf[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

bool ToolRegistry::Load(const std::string& jsonPath) {
    std::ifstream f(jsonPath);
    if (!f.is_open()) return false;
    try {
        nlohmann::json j;
        f >> j;
        if (!j.contains("tools") || !j["tools"].is_array()) return false;
        tools_.clear();
        for (const auto& e : j["tools"]) {
            Tool t;
            t.name              = e.value("name", std::string());
            t.type              = e.value("type", std::string("web"));
            t.openUrl           = e.value("openUrl", std::string());
            t.searchUrlTemplate = e.value("searchUrlTemplate", std::string());
            t.exe               = e.value("exe", std::string());
            if (e.contains("aliases") && e["aliases"].is_array()) {
                for (const auto& a : e["aliases"]) {
                    if (a.is_string()) t.aliases.push_back(a.get<std::string>());
                }
            }
            if (!t.name.empty()) tools_.push_back(std::move(t));
        }
    } catch (...) {
        return false;
    }
    return !tools_.empty();
}

void ToolRegistry::Register(const Tool& tool) {
    tools_.push_back(tool);
}

const Tool* ToolRegistry::Find(const std::string& target) const {
    if (target.empty()) return nullptr;
    const std::string key = ToLower(target);

    // 1) 正規名の完全一致
    for (const auto& t : tools_) {
        if (ToLower(t.name) == key) return &t;
    }
    // 2) 別名の完全一致
    for (const auto& t : tools_) {
        for (const auto& a : t.aliases) {
            if (ToLower(a) == key) return &t;
        }
    }
    // 3) 部分一致（名前/別名が target に含まれる）。短いトークン（"x","yt" 等）は
    //    誤マッチ（"netflix"→"x" 等）を避けるため 3 バイト以上のみ対象とする。
    for (const auto& t : tools_) {
        const std::string ln = ToLower(t.name);
        if (ln.size() >= 3 && key.find(ln) != std::string::npos) return &t;
        for (const auto& a : t.aliases) {
            const std::string la = ToLower(a);
            if (la.size() >= 3 && key.find(la) != std::string::npos) return &t;
        }
    }
    return nullptr;
}

std::string ToolRegistry::BuildSearchUrl(const Tool& tool, const std::string& query) {
    const std::string enc = UrlEncode(query);
    const std::string& tmpl = tool.searchUrlTemplate;
    const std::string token = "{query}";
    std::string out;
    size_t i = 0;
    while (i < tmpl.size()) {
        const size_t p = tmpl.find(token, i);
        if (p == std::string::npos) { out += tmpl.substr(i); break; }
        out += tmpl.substr(i, p - i);
        out += enc;
        i = p + token.size();
    }
    return out;
}

std::string ToolRegistry::BuildToolListForPrompt() const {
    std::ostringstream os;
    for (const auto& t : tools_) {
        os << "- " << t.name << " (" << t.type << ")";
        if (!t.aliases.empty()) {
            os << " [";
            for (size_t i = 0; i < t.aliases.size(); ++i) {
                if (i) os << ", ";
                os << t.aliases[i];
            }
            os << "]";
        }
        os << "\n";
    }
    return os.str();
}
