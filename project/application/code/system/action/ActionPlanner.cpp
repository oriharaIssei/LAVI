#include "ActionPlanner.h"

#include <cstdio>

namespace {

struct ServiceUrl {
    const char* name;
    const char* searchFmt;
};

static const ServiceUrl kServiceUrls[] = {
    {"YouTube",       "https://www.youtube.com/results?search_query=%s"},
    {"Spotify",       "https://open.spotify.com/search/%s"},
    {"niconico",      "https://www.nicovideo.jp/search/%s"},
    {"Twitch",        "https://www.twitch.tv/search?term=%s"},
    {"Twitter/X",     "https://x.com/search?q=%s"},
    {"GitHub",        "https://github.com/search?q=%s"},
    {"Amazon",        "https://www.amazon.co.jp/s?k=%s"},
    {"Qiita",         "https://qiita.com/search?q=%s"},
    {"Zenn",          "https://zenn.dev/search?q=%s"},
    {"note",          "https://note.com/search?q=%s"},
    {"StackOverflow", "https://stackoverflow.com/search?q=%s"},
    {"Reddit",        "https://www.reddit.com/search/?q=%s"},
    {"Wikipedia",     "https://ja.wikipedia.org/w/index.php?search=%s"},
    {"Pixiv",         "https://www.pixiv.net/tags/%s"},
    {"Google",        "https://www.google.com/search?q=%s"},
};

std::string UrlEncode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

std::string FormatSearchUrl(const char* fmt, const std::string& query) {
    std::string encoded = UrlEncode(query);
    std::string url;
    for (const char* p = fmt; *p; ++p) {
        if (*p == '%' && *(p + 1) == 's') {
            url += encoded;
            ++p;
        } else {
            url += *p;
        }
    }
    return url;
}

const char* FindSearchUrl(const std::string& name) {
    for (auto& su : kServiceUrls) {
        if (name == su.name) return su.searchFmt;
    }
    return nullptr;
}

} // namespace

std::vector<ActionCommand> ActionPlanner::Plan(
    const IntentResult& intent,
    const CapabilityResult& capability,
    const UserPreference& preference,
    const std::string& serviceName,
    const std::vector<std::string>& keywords) const {

    std::vector<ActionCommand> commands;

    if (capability.type == CapabilityType::ApplicationLaunch) {
        std::string target = intent.targetName.empty() ? intent.query : intent.targetName;
        if (target.empty()) return commands;

        ActionCommand cmd;
        cmd.type = ActionType::LaunchApplication;
        cmd.parameter = target;
        cmd.label = intent.query.empty() ? target : intent.query;
        commands.push_back(std::move(cmd));
        return commands;
    }

    std::string query;
    for (auto& kw : keywords) {
        if (!query.empty()) query += " ";
        query += kw;
    }

    if (intent.type == IntentType::PlayMusic && !query.empty()) {
        query += " MIX";
    }
    if (query.empty()) query = "trending";

    const char* urlFmt = FindSearchUrl(serviceName);
    if (!urlFmt) urlFmt = FindSearchUrl("Google");
    if (!urlFmt) return commands;

    ActionCommand cmd;
    cmd.type = ActionType::SearchUrl;
    cmd.parameter = FormatSearchUrl(urlFmt, query);
    cmd.browserProcess = preference.preferredBrowser;
    cmd.label = serviceName + ": " + query;

    commands.push_back(std::move(cmd));
    return commands;
}
