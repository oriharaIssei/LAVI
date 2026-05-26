#include "IntentExtractor.h"

#include <cstring>
#include <fstream>
#include <sstream>

namespace {

struct ApplicationAlias {
    const char* keyword;
    const char* executable;
    const char* label;
};

static const ApplicationAlias kApplicationAliases[] = {
    {"notepad", "notepad.exe", "notepad"},
    {"メモ帳", "notepad.exe", "メモ帳"},
    {"電卓", "calc.exe", "電卓"},
    {"calculator", "calc.exe", "calculator"},
    {"calc", "calc.exe", "calc"},
    {"ペイント", "mspaint.exe", "ペイント"},
    {"paint", "mspaint.exe", "paint"},
    {"エクスプローラー", "explorer.exe", "エクスプローラー"},
    {"explorer", "explorer.exe", "explorer"},
    {"ターミナル", "wt.exe", "ターミナル"},
    {"terminal", "wt.exe", "terminal"},
    {"コマンドプロンプト", "cmd.exe", "コマンドプロンプト"},
    {"cmd", "cmd.exe", "cmd"},
    {"PowerShell", "powershell.exe", "PowerShell"},
    {"powershell", "powershell.exe", "powershell"},
    {"VS Code", "code.exe", "VS Code"},
    {"vscode", "code.exe", "vscode"},
    {"Visual Studio", "devenv.exe", "Visual Studio"},
    {"Discord", "Discord.exe", "Discord"},
    {"discord", "Discord.exe", "discord"},
    {"Slack", "slack.exe", "Slack"},
    {"slack", "slack.exe", "slack"},
    {"Steam", "steam.exe", "Steam"},
    {"steam", "steam.exe", "steam"},
};

std::vector<std::string> ParseJsonStringArray(const std::string& arr) {
    std::vector<std::string> result;
    size_t cur = 0;
    while (cur < arr.size()) {
        auto q1 = arr.find('"', cur);
        if (q1 == std::string::npos) break;
        auto q2 = arr.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        result.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
        cur = q2 + 1;
    }
    return result;
}

} // namespace

bool IntentExtractor::LoadConfig(const std::string& tagAxesPath) {
    std::ifstream ifs(tagAxesPath, std::ios::binary);
    if (!ifs) return false;
    std::string json((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    interestCategories_.clear();
    allTags_.clear();
    synonyms_.clear();

    // interest_categories
    auto icPos = json.find("\"interest_categories\"");
    if (icPos != std::string::npos) {
        auto icArr = json.find('[', icPos);
        if (icArr != std::string::npos) {
            auto icEnd = json.find(']', icArr);
            if (icEnd != std::string::npos) {
                interestCategories_ = ParseJsonStringArray(
                    json.substr(icArr + 1, icEnd - icArr - 1));
            }
        }
    }

    // axes -> allTags_
    auto axesPos = json.find("\"axes\"");
    if (axesPos != std::string::npos) {
        auto axesBrace = json.find('{', axesPos + 6);
        if (axesBrace != std::string::npos) {
            size_t cur = axesBrace + 1;
            while (cur < json.size()) {
                auto keyStart = json.find('"', cur);
                if (keyStart == std::string::npos) break;
                auto keyEnd = json.find('"', keyStart + 1);
                if (keyEnd == std::string::npos) break;
                auto closeBrace = json.find('}', cur);
                if (closeBrace != std::string::npos && closeBrace < keyStart) break;
                auto arrStart = json.find('[', keyEnd);
                if (arrStart == std::string::npos) break;
                auto arrEnd = json.find(']', arrStart);
                if (arrEnd == std::string::npos) break;
                auto tags = ParseJsonStringArray(
                    json.substr(arrStart + 1, arrEnd - arrStart - 1));
                for (auto& t : tags) allTags_.push_back(t);
                cur = arrEnd + 1;
            }
        }
    }

    // synonyms
    auto synPos = json.find("\"synonyms\"");
    if (synPos != std::string::npos) {
        auto synBrace = json.find('{', synPos + 10);
        if (synBrace != std::string::npos) {
            auto synClose = json.find('}', synBrace);
            if (synClose != std::string::npos) {
                size_t sc = synBrace + 1;
                while (sc < synClose) {
                    auto k1 = json.find('"', sc);
                    if (k1 == std::string::npos || k1 >= synClose) break;
                    auto k2 = json.find('"', k1 + 1);
                    if (k2 == std::string::npos) break;
                    auto v1 = json.find('"', k2 + 1);
                    if (v1 == std::string::npos) break;
                    auto v2 = json.find('"', v1 + 1);
                    if (v2 == std::string::npos) break;
                    synonyms_[json.substr(k1 + 1, k2 - k1 - 1)] =
                        json.substr(v1 + 1, v2 - v1 - 1);
                    sc = v2 + 1;
                }
            }
        }
    }

    BuildRules();
    return true;
}

void IntentExtractor::BuildRules() {
    rules_.clear();

    rules_.push_back({IntentType::PlayMusic, {
        "\xE9\x9F\xB3\xE6\xA5\xBD",                     // 音楽
        "\xE6\x9B\xB2",                                   // 曲
        "\xE6\xAD\x8C",                                   // 歌
        "BGM",
        "\xE5\x86\x8D\xE7\x94\x9F",                     // 再生
        "\xE8\x81\xB4",                                   // 聴
    }});

    rules_.push_back({IntentType::SearchWeb, {
        "\xE8\xAA\xBF\xE3\x81\xB9",                     // 調べ
        "\xE6\xA4\x9C\xE7\xB4\xA2",                     // 検索
        "\xE6\x8E\xA2\xE3\x81\x97\xE3\x81\xA6",       // 探して
    }});

    rules_.push_back({IntentType::OpenWebsite, {
        "\xE9\x96\x8B\xE3\x81\x84\xE3\x81\xA6",       // 開いて
        "\xE8\xA6\x8B\xE3\x81\x9B\xE3\x81\xA6",       // 見せて
        "\xE3\x81\xA4\xE3\x81\x91\xE3\x81\xA6",       // つけて
    }});

    rules_.push_back({IntentType::OpenApplication, {
        "\xE8\xB5\xB7\xE5\x8B\x95",                   // 起動
        "\xE7\xAB\x8B\xE3\x81\xA1\xE4\xB8\x8A\xE3\x81\x92", // 立ち上げ
        "\xE3\x82\xA2\xE3\x83\x97\xE3\x83\xAA",       // アプリ
        "\xE3\x82\xBD\xE3\x83\x95\xE3\x83\x88",       // ソフト
    }});
}

IntentResult IntentExtractor::Extract(const std::string& speech) const {
    IntentResult result;
    result.rawText = speech;

    if (TryExtractApplication(speech, result)) return result;

    // Priority-ordered rule matching
    for (auto& rule : rules_) {
        for (auto& kw : rule.keywords) {
            if (speech.find(kw) != std::string::npos) {
                result.type = rule.type;
                result.confidence = 0.8f;
                result.query = ExtractSubject(speech);
                if (rule.type == IntentType::OpenApplication) {
                    result.targetName = result.query;
                }
                return result;
            }
        }
    }

    // Action verb fallback
    static const char* kActionKw[] = {
        "\xE3\x81\x8B\xE3\x81\x91\xE3\x81\xA6",       // かけて
        "\xE6\xB5\x81\xE3\x81\x97\xE3\x81\xA6",       // 流して
        "\xE8\x81\x9E\xE3\x81\x8B\xE3\x81\x9B\xE3\x81\xA6", // 聞かせて
        "\xE5\x8B\x95\xE7\x94\xBB",                     // 動画
    };
    for (auto* kw : kActionKw) {
        if (speech.find(kw) != std::string::npos) {
            result.type = IntentType::OpenWebsite;
            result.confidence = 0.6f;
            result.query = ExtractSubject(speech);
            return result;
        }
    }

    // Explicit service name detection
    static const char* kServices[] = {
        "YouTube", "youtube", "Spotify", "spotify",
        "Twitter", "twitter", "GitHub", "github",
    };
    for (auto* s : kServices) {
        if (speech.find(s) != std::string::npos) {
            result.type = IntentType::OpenWebsite;
            result.targetName = s;
            result.confidence = 0.9f;
            result.query = ExtractSubject(speech);
            return result;
        }
    }

    return result;
}

bool IntentExtractor::TryExtractApplication(const std::string& speech, IntentResult& result) const {
    bool hasLaunchVerb =
        speech.find("\xE9\x96\x8B\xE3\x81\x84\xE3\x81\xA6") != std::string::npos || // 開いて
        speech.find("\xE8\xB5\xB7\xE5\x8B\x95") != std::string::npos ||             // 起動
        speech.find("\xE7\xAB\x8B\xE3\x81\xA1\xE4\xB8\x8A\xE3\x81\x92") != std::string::npos || // 立ち上げ
        speech.find("\xE3\x81\xA4\xE3\x81\x91\xE3\x81\xA6") != std::string::npos;   // つけて

    if (!hasLaunchVerb) return false;

    for (auto& app : kApplicationAliases) {
        if (speech.find(app.keyword) == std::string::npos) continue;

        result.type = IntentType::OpenApplication;
        result.rawText = speech;
        result.targetName = app.executable;
        result.query = app.label;
        result.confidence = 0.9f;
        return true;
    }

    return false;
}

bool IntentExtractor::ContainsActionKeyword(const std::string& text) const {
    return Extract(text).type != IntentType::None;
}

std::string IntentExtractor::ExtractSubject(const std::string& speech) const {
    std::string subject = speech;

    for (auto& tag : allTags_) {
        size_t pos;
        while ((pos = subject.find(tag)) != std::string::npos)
            subject.erase(pos, tag.size());
    }
    for (auto& [word, _] : synonyms_) {
        size_t pos;
        while ((pos = subject.find(word)) != std::string::npos)
            subject.erase(pos, word.size());
    }

    static const char* kStop[] = {
        "\xe8\xaa\xbf\xe3\x81\xb9",                     // 調べ
        "\xe6\xa4\x9c\xe7\xb4\xa2",                     // 検索
        "\xe6\xb5\x81\xe3\x81\x97\xe3\x81\xa6",       // 流して
        "\xe9\x96\x8b\xe3\x81\x84\xe3\x81\xa6",       // 開いて
        "\xe8\xa6\x8b\xe3\x81\x9b\xe3\x81\xa6",       // 見せて
        "\xe6\x8e\xa2\xe3\x81\x97\xe3\x81\xa6",       // 探して
        "\xe3\x81\x8b\xe3\x81\x91\xe3\x81\xa6",       // かけて
        "\xe5\x86\x8d\xe7\x94\x9f",                     // 再生
        "\xe8\x81\x9e\xe3\x81\x8b\xe3\x81\x9b\xe3\x81\xa6", // 聞かせて
        "\xe3\x81\xa4\xe3\x81\x91\xe3\x81\xa6",       // つけて
        "\xe8\x81\xb4\xe3\x81\x8d\xe3\x81\x9f\xe3\x81\x84", // 聴きたい
        "\xe8\xa6\x8b\xe3\x81\x9f\xe3\x81\x84",       // 見たい
        "\xe3\x81\x97\xe3\x81\x9f\xe3\x81\x84",       // したい
        "\xe3\x81\xbb\xe3\x81\x97\xe3\x81\x84",       // ほしい
        "\xe3\x81\xae", "\xe3\x82\x92", "\xe3\x81\xab",
        "\xe3\x81\x8c", "\xe3\x81\xa7", "\xe3\x81\xaf",
        "\xe3\x81\xa8", "\xe3\x82\x82",
    };
    for (auto* w : kStop) {
        size_t pos;
        while ((pos = subject.find(w)) != std::string::npos)
            subject.erase(pos, strlen(w));
    }

    while (!subject.empty() && (subject.front() == ' ' || subject.front() == '\t'))
        subject.erase(0, 1);
    while (!subject.empty() &&
           (subject.back() == ' ' || subject.back() == '\t' || subject.back() == '?'))
        subject.pop_back();

    return subject;
}
