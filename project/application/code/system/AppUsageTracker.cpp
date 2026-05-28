#include "AppUsageTracker.h"
#include "InterestGraph.h"
#include "LocalLLM.h"
#include "LongTermMemory.h"

#define ENGINE_INCLUDE
#define ENGINE_PROCESS_MANAGER
#include <EngineInclude.h>

#include "util/StringUtil.h"

#include <algorithm>
#include <ctime>
#include <sstream>

namespace {

const std::unordered_set<std::string> kBrowserProcesses = {
    "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe",
    "opera.exe", "vivaldi.exe", "iexplore.exe", "Arc.exe"
};

struct ServiceKeyword {
    const char* keyword;
    const char* serviceName;
    std::vector<std::string> defaultTags;
};

// タグ体系 (3軸, 各サービスに1つずつ)
//   目的: 開発, 仕事, 娯楽, 学習, 交流, 買い物
//   形式: 動画, 音楽, テキスト, 画像, チャット, コード, スライド, ツール
//   関与: 閲覧, 発信, 作業, 対話
const ServiceKeyword kServiceKeywords[] = {
    {"YouTube",        "YouTube",       {"娯楽", "動画", "音楽", "閲覧"}},
    {"Twitter",        "Twitter/X",     {"交流", "テキスト", "発信"}},
    {" X ",            "Twitter/X",     {"交流", "テキスト", "発信"}},
    {"x.com",          "Twitter/X",     {"交流", "テキスト", "発信"}},
    {"GitHub",         "GitHub",        {"開発", "コード", "作業"}},
    {"Stack Overflow", "StackOverflow", {"開発", "テキスト", "閲覧"}},
    {"Reddit",         "Reddit",        {"交流", "テキスト", "閲覧"}},
    {"Discord",        "Discord",       {"交流", "チャット", "対話"}},
    {"Slack",          "Slack",         {"仕事", "チャット", "対話"}},
    {"Gmail",          "Gmail",         {"仕事", "テキスト", "対話"}},
    {"Outlook",        "Outlook",       {"仕事", "テキスト", "対話"}},
    {"Amazon",         "Amazon",        {"買い物", "テキスト", "閲覧"}},
    {"Google Drive",   "GoogleDrive",   {"仕事", "ツール", "作業"}},
    {"Google Docs",    "GoogleDocs",    {"仕事", "テキスト", "作業"}},
    {"Notion",         "Notion",        {"仕事", "テキスト", "作業"}},
    {"Twitch",         "Twitch",        {"娯楽", "動画", "閲覧"}},
    {"Netflix",        "Netflix",       {"娯楽", "動画", "閲覧"}},
    {"Spotify",        "Spotify",       {"娯楽", "音楽", "閲覧"}},
    {"Instagram",      "Instagram",     {"交流", "画像", "発信"}},
    {"Facebook",       "Facebook",      {"交流", "テキスト", "発信"}},
    {"LINE",           "LINE",          {"交流", "チャット", "対話"}},
    {"ChatGPT",        "ChatGPT",       {"開発", "テキスト", "対話"}},
    {"Claude",         "Claude",        {"開発", "テキスト", "対話"}},
    {"Wikipedia",      "Wikipedia",     {"学習", "テキスト", "閲覧"}},
    {"Qiita",          "Qiita",         {"開発", "テキスト", "閲覧"}},
    {"Zenn",           "Zenn",          {"開発", "テキスト", "閲覧"}},
    {"niconico",       "niconico",      {"娯楽", "動画", "閲覧"}},
    {"\xe3\x83\x8b\xe3\x82\xb3\xe3\x83\x8b\xe3\x82\xb3", "niconico", {"娯楽", "動画", "閲覧"}},
    {"Pixiv",          "Pixiv",         {"娯楽", "画像", "閲覧"}},
    {"\xef\xbd\x9cnote", "note",         {"交流", "テキスト", "発信"}},
    {"note.com",       "note",          {"交流", "テキスト", "発信"}},
    {"Docswell",       "Docswell",      {"学習", "スライド", "閲覧"}},
    {"\xe3\x83\x89\xe3\x82\xaf\xe3\x82\xbb\xe3\x83\xab", "Docswell", {"学習", "スライド", "閲覧"}},
    {"4Gamer",         "4Gamer",        {"娯楽", "テキスト", "閲覧"}},
    {"Hatena",         "はてな",         {"開発", "テキスト", "閲覧"}},
    {"\xe3\x81\xaf\xe3\x81\xa6\xe3\x81\xaa", "はてな", {"開発", "テキスト", "閲覧"}},
    {"Connpass",       "Connpass",      {"開発", "ツール", "閲覧"}},
    {"AtCoder",        "AtCoder",       {"娯楽", "コード", "作業"}},
    {"Google Maps",    "GoogleMaps",    {"仕事", "ツール", "閲覧"}},
    {"Google Calendar","GoogleCalendar",{"仕事", "ツール", "作業"}},
};

const std::unordered_set<std::string> kIgnoredProcesses = {
    "explorer.exe", "SearchHost.exe", "ShellExperienceHost.exe",
    "StartMenuExperienceHost.exe", "TextInputHost.exe",
    "SystemSettings.exe", "ApplicationFrameHost.exe",
    "dwm.exe", "csrss.exe", "winlogon.exe", "taskhostw.exe",
    "RuntimeBroker.exe", "svchost.exe", "conhost.exe",
    "SearchUI.exe", "LockApp.exe", "SecurityHealthSystray.exe",
    "LAVI.exe"
};

bool ShouldIgnore(const std::string& processName) {
    if (processName.empty()) return true;
    return kIgnoredProcesses.count(processName) > 0;
}

}  // namespace

AppUsageTracker::AppUsageTracker() = default;
AppUsageTracker::~AppUsageTracker() = default;

void AppUsageTracker::Initialize(LongTermMemory* ltm, LocalLLM* llm, const std::string& apiKey,
                                 InterestGraph* ig) {
    ltm_ = ltm;
    llm_ = llm;
    ig_ = ig;

    if (!apiKey.empty()) {
        cloudLlm_.SetApiKey(apiKey);
        cloudLlm_.SetModel("claude-haiku-4-5-20251001");
        cloudLlm_.SetMaxTokens(256);
        cloudLlm_.SetEnableWebSearch(true, 2);
    }

    for (auto& rec : ltm_->GetAppUsageRecords()) {
        everSeenApps_.insert(rec.processName);
    }
}

void AppUsageTracker::Update(float deltaTime) {
    if (!ltm_) return;

    PollTagging();

    scanTimer_ += deltaTime;
    if (scanTimer_ >= scanInterval_) {
        scanTimer_ = 0.0f;
        ScanProcesses();
    }

    parallelScanTimer_ += deltaTime;
    if (parallelScanTimer_ >= parallelScanInterval_) {
        parallelScanTimer_ = 0.0f;
        DetectParallelUsage();
    }
}

void AppUsageTracker::Finalize() {
    if (!currentWebService_.empty()) {
        auto now = std::chrono::steady_clock::now();
        float minutes = std::chrono::duration<float>(now - webServiceStart_).count() / 60.0f;
        if (minutes > 0.01f && ltm_) {
            ltm_->RecordWebService(currentWebService_, currentForeground_, minutes);
        }
        currentWebService_.clear();
    }

    for (auto& [name, app] : runningApps_) {
        CloseSegment(app);
        FlushApp(app);
    }
    runningApps_.clear();
    currentForeground_.clear();

    if (localTagFuture_.valid()) localTagFuture_.wait();
    if (cloudTagFuture_.valid()) cloudTagFuture_.wait();
    PollTagging();
}

void AppUsageTracker::CloseSegment(TrackedApp& app) {
    auto now = std::chrono::steady_clock::now();
    float minutes = std::chrono::duration<float>(now - app.segmentStart).count() / 60.0f;
    if (app.isForeground) {
        app.accumulatedFgMinutes += minutes;
    } else {
        app.accumulatedBgMinutes += minutes;
    }
    app.segmentStart = now;
}

void AppUsageTracker::FlushApp(TrackedApp& app) {
    if (app.accumulatedFgMinutes > 0.01f) {
        ltm_->RecordAppForeground(app.processName, app.windowTitle, app.accumulatedFgMinutes);
    }
    if (app.accumulatedBgMinutes > 0.01f) {
        ltm_->RecordAppBackground(app.processName, app.windowTitle, app.accumulatedBgMinutes);
    }
}

void AppUsageTracker::ScanProcesses() {
    auto fgInfo = OriGine::ProcessManager::GetForegroundApp();
    std::string fgApp = ConvertString(fgInfo.exeName);
    std::string fgTitle = ConvertString(fgInfo.windowTitle);
    if (ShouldIgnore(fgApp)) fgApp.clear();

    auto now = std::chrono::steady_clock::now();

    if (fgApp != currentForeground_) {
        if (!currentForeground_.empty() && !fgApp.empty()) {
            ltm_->RecordAppTransition(currentForeground_, fgApp);
        }
        if (!currentForeground_.empty()) {
            auto it = runningApps_.find(currentForeground_);
            if (it != runningApps_.end()) {
                CloseSegment(it->second);
                it->second.isForeground = false;
                it->second.segmentStart = now;
            }
        }
        if (!fgApp.empty()) {
            auto it = runningApps_.find(fgApp);
            if (it != runningApps_.end()) {
                CloseSegment(it->second);
                it->second.isForeground = true;
                it->second.segmentStart = now;
                it->second.windowTitle = fgTitle;
            }
        }
        if (!IsBrowser(fgApp)) {
            if (!currentWebService_.empty()) {
                float minutes = std::chrono::duration<float>(now - webServiceStart_).count() / 60.0f;
                if (minutes > 0.01f) {
                    ltm_->RecordWebService(currentWebService_, currentForeground_, minutes);
                }
                currentWebService_.clear();
            }
        }
        currentForeground_ = fgApp;
    }

    if (IsBrowser(fgApp)) {
        DetectWebService(fgApp, fgTitle);
    }

    auto windows = OriGine::ProcessManager::EnumerateWindows();
    std::unordered_set<std::string> currentSet;
    for (auto& w : windows) {
        std::string exe = ConvertString(w.exeName);
        if (ShouldIgnore(exe)) continue;
        currentSet.insert(exe);

        if (runningApps_.find(exe) == runningApps_.end()) {
            TrackedApp ta;
            ta.processName = exe;
            ta.windowTitle = ConvertString(w.windowTitle);
            ta.openTime = now;
            ta.segmentStart = now;
            ta.isForeground = (exe == currentForeground_);
            runningApps_[exe] = std::move(ta);

            if (!currentForeground_.empty() && exe != currentForeground_) {
                ltm_->RecordAppLaunch(exe, currentForeground_);
            }

            if (everSeenApps_.find(exe) == everSeenApps_.end()) {
                everSeenApps_.insert(exe);
                EnqueueTagging(exe, runningApps_[exe].windowTitle);
            }
        }
    }

    std::vector<std::string> closed;
    for (auto& [name, app] : runningApps_) {
        if (currentSet.find(name) == currentSet.end()) {
            closed.push_back(name);
        }
    }
    for (auto& name : closed) {
        auto& app = runningApps_[name];
        CloseSegment(app);
        FlushApp(app);
        if (name == currentForeground_) {
            currentForeground_.clear();
        }
        runningApps_.erase(name);
    }
}

bool AppUsageTracker::IsBrowser(const std::string& processName) {
    return kBrowserProcesses.count(processName) > 0;
}

std::string AppUsageTracker::ExtractServiceName(const std::string& windowTitle) {
    for (auto& sk : kServiceKeywords) {
        if (windowTitle.find(sk.keyword) != std::string::npos) {
            return sk.serviceName;
        }
    }
    return "";
}

void AppUsageTracker::DetectWebService(const std::string& browserProcess, const std::string& windowTitle) {
    std::string service = ExtractServiceName(windowTitle);

    if (service != currentWebService_) {
        if (!currentWebService_.empty()) {
            auto now = std::chrono::steady_clock::now();
            float minutes = std::chrono::duration<float>(now - webServiceStart_).count() / 60.0f;
            if (minutes > 0.01f) {
                ltm_->RecordWebService(currentWebService_, browserProcess, minutes);
            }
        }
        currentWebService_ = service;
        webServiceStart_ = std::chrono::steady_clock::now();

        if (!service.empty()) {
            auto* ws = ltm_->FindWebService(service);
            if (ws && ws->tags.empty()) {
                for (auto& sk : kServiceKeywords) {
                    if (sk.serviceName == service && !sk.defaultTags.empty()) {
                        ltm_->SetWebServiceTags(service, sk.defaultTags);
                        break;
                    }
                }
            }
        }
    }
}

std::string AppUsageTracker::BuildLocalTagPrompt(const std::string& processName, const std::string& windowTitle) {
    return
        "あなたはWindowsアプリ分類の専門家です。\n\n"
        "プロセス名: " + processName + "\n"
        "ウィンドウタイトル: " + windowTitle + "\n\n"
        "【ルール】\n"
        "- 確実に知っているアプリなら、3つのタグをカンマ区切りで回答\n"
        "- 知らないアプリ、自信がない場合は「不明」とだけ回答\n"
        "- 3つの軸から1つずつ選ぶ:\n"
        "  目的: 開発, 仕事, 娯楽, 学習, 交流, 買い物\n"
        "  形式: 動画, 音楽, テキスト, 画像, チャット, コード, スライド, ツール\n"
        "  関与: 閲覧, 発信, 作業, 対話\n"
        "- 余計な説明は絶対に書かないこと\n\n"
        "【回答例】\n"
        "devenv.exe → 開発, コード, 作業\n"
        "知らないアプリ → 不明\n\n"
        "回答:";
}

std::string AppUsageTracker::BuildCloudTagPrompt(const std::string& processName, const std::string& windowTitle) {
    return
        "以下のWindowsアプリケーションが何のソフトか調べて、3つのカテゴリタグをつけてください。\n"
        "必要であればWeb検索してください。\n\n"
        "プロセス名: " + processName + "\n"
        "ウィンドウタイトル: " + windowTitle + "\n\n"
        "3つの軸から1つずつ選んでください:\n"
        "  目的: 開発, 仕事, 娯楽, 学習, 交流, 買い物\n"
        "  形式: 動画, 音楽, テキスト, 画像, チャット, コード, スライド, ツール\n"
        "  関与: 閲覧, 発信, 作業, 対話\n\n"
        "回答はタグのみをカンマ区切りで返してください。余計な説明は不要です。\n"
        "どうしても分からない場合のみ「不明」と回答。";
}

void AppUsageTracker::EnqueueTagging(const std::string& processName, const std::string& windowTitle) {
    tagQueue_.push_back({processName, windowTitle});
    if (tagStage_ == TagStage::None) {
        StartNextTagging();
    }
}

void AppUsageTracker::StartNextTagging() {
    if (tagQueue_.empty()) {
        tagStage_ = TagStage::None;
        return;
    }

    auto req = tagQueue_.front();
    tagQueue_.erase(tagQueue_.begin());
    pendingTagProcess_ = req.processName;
    pendingTagTitle_ = req.windowTitle;

    if (llm_ && llm_->IsModelLoaded() && !llm_->IsProcessing()) {
        tagStage_ = TagStage::LocalLLM;
        std::string prompt = BuildLocalTagPrompt(pendingTagProcess_, pendingTagTitle_);
        localTagFuture_ = llm_->GenerateAsync(prompt);
        return;
    }

    if (!cloudLlm_.IsProcessing()) {
        tagStage_ = TagStage::CloudAPI;
        std::string prompt = BuildCloudTagPrompt(pendingTagProcess_, pendingTagTitle_);
        cloudLlm_.ClearHistory();
        cloudLlm_.AddMessage("user", prompt);
        cloudTagFuture_ = cloudLlm_.SendAsync();
        return;
    }

    tagQueue_.insert(tagQueue_.begin(), req);
    tagStage_ = TagStage::None;
}

void AppUsageTracker::PollTagging() {
    if (tagStage_ == TagStage::LocalLLM && localTagFuture_.valid()) {
        if (localTagFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

        std::string response = localTagFuture_.get();

        if (IsUnknownResponse(response)) {
            // ローカル LLM が「不明」→ Claude API + Web検索にフォールバック
            if (!cloudLlm_.IsProcessing()) {
                tagStage_ = TagStage::CloudAPI;
                std::string prompt = BuildCloudTagPrompt(pendingTagProcess_, pendingTagTitle_);
                cloudLlm_.ClearHistory();
                cloudLlm_.AddMessage("user", prompt);
                cloudTagFuture_ = cloudLlm_.SendAsync();
            } else {
                tagQueue_.push_back({pendingTagProcess_, pendingTagTitle_});
                pendingTagProcess_.clear();
                pendingTagTitle_.clear();
                StartNextTagging();
            }
            return;
        }

        auto tags = ParseTags(response);
        if (!tags.empty()) {
            auto* rec = ltm_->FindAppRecord(pendingTagProcess_);
            if (rec && rec->tags.empty()) {
                ltm_->SetAppTags(pendingTagProcess_, tags);
            }
            pendingTagProcess_.clear();
            pendingTagTitle_.clear();
            StartNextTagging();
        } else {
            // パース失敗 → Claude にフォールバック
            if (!cloudLlm_.IsProcessing()) {
                tagStage_ = TagStage::CloudAPI;
                std::string prompt = BuildCloudTagPrompt(pendingTagProcess_, pendingTagTitle_);
                cloudLlm_.ClearHistory();
                cloudLlm_.AddMessage("user", prompt);
                cloudTagFuture_ = cloudLlm_.SendAsync();
            } else {
                tagQueue_.push_back({pendingTagProcess_, pendingTagTitle_});
                pendingTagProcess_.clear();
                pendingTagTitle_.clear();
                StartNextTagging();
            }
        }
        return;
    }

    if (tagStage_ == TagStage::CloudAPI && cloudTagFuture_.valid()) {
        if (cloudTagFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

        LLMResponse resp = cloudTagFuture_.get();
        if (resp.success && !IsUnknownResponse(resp.content)) {
            auto tags = ParseTags(resp.content);
            if (!tags.empty()) {
                auto* rec = ltm_->FindAppRecord(pendingTagProcess_);
                if (rec && rec->tags.empty()) {
                    ltm_->SetAppTags(pendingTagProcess_, tags);
                }
            }
        }
        pendingTagProcess_.clear();
        pendingTagTitle_.clear();
        StartNextTagging();
    }
}

std::vector<std::string> AppUsageTracker::ParseTags(const std::string& response) const {
    std::vector<std::string> tags;
    std::istringstream ss(response);
    std::string token;
    while (std::getline(ss, token, ',')) {
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            std::string tag = token.substr(start, end - start + 1);
            if (!tag.empty() && tag.size() < 30) {
                tags.push_back(tag);
            }
        }
    }
    if (tags.size() > 5) tags.resize(5);
    return tags;
}

bool AppUsageTracker::IsUnknownResponse(const std::string& response) const {
    std::string trimmed = response;
    auto start = trimmed.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return true;
    trimmed = trimmed.substr(start);

    if (trimmed.find("\xe4\xb8\x8d\xe6\x98\x8e") != std::string::npos) return true;  // "不明" UTF-8
    if (trimmed.find("\xe5\x88\x86\xe3\x81\x8b\xe3\x82\x89\xe3\x81\xaa\xe3\x81\x84") != std::string::npos) return true;  // "分からない"
    if (trimmed.find("\xe3\x82\x8f\xe3\x81\x8b\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x9b\xe3\x82\x93") != std::string::npos) return true;  // "わかりません"
    if (trimmed.size() > 200) return true;  // 長すぎる回答は信頼できない

    return false;
}

bool AppUsageTracker::IsTagging() const {
    return tagStage_ != TagStage::None;
}

std::vector<AppUsageTracker::ActiveAppInfo> AppUsageTracker::GetActiveApps() const {
    std::vector<ActiveAppInfo> result;
    auto now = std::chrono::steady_clock::now();
    for (auto& [name, app] : runningApps_) {
        ActiveAppInfo info;
        info.processName = name;
        info.windowTitle = app.windowTitle;
        info.isForeground = app.isForeground;
        info.sessionMinutes = std::chrono::duration<float>(now - app.openTime).count() / 60.0f;
        result.push_back(std::move(info));
    }
    std::sort(result.begin(), result.end(),
        [](const ActiveAppInfo& a, const ActiveAppInfo& b) {
            if (a.isForeground != b.isForeground) return a.isForeground;
            return a.sessionMinutes > b.sessionMinutes;
        });
    return result;
}

std::string AppUsageTracker::ClassifyContext(const std::string& processName, const std::string& windowTitle) {
    if (IsBrowser(processName)) {
        std::string service = ExtractServiceName(windowTitle);
        if (!service.empty()) return service;
        return "Web";
    }
    static const std::unordered_map<std::string, std::string> kContextMap = {
        {"devenv.exe", "IDE"},
        {"Code.exe", "IDE"},
        {"rider64.exe", "IDE"},
        {"clion64.exe", "IDE"},
        {"WindowsTerminal.exe", "Terminal"},
        {"cmd.exe", "Terminal"},
        {"powershell.exe", "Terminal"},
        {"blender.exe", "3DCG"},
        {"photoshop.exe", "Design"},
        {"clip_studio_paint.exe", "Drawing"},
        {"Discord.exe", "Chat"},
        {"vesktop.exe", "Chat"},
        {"slack.exe", "Chat"},
        {"Spotify.exe", "Music"},
        {"steam.exe", "Game"},
        {"steamwebhelper.exe", "Game"},
    };
    auto it = kContextMap.find(processName);
    if (it != kContextMap.end()) return it->second;
    return "";
}

void AppUsageTracker::DetectParallelUsage() {
    if (currentForeground_.empty()) return;

    auto now = std::chrono::steady_clock::now();

    std::string fgTitle;
    auto fgIt = runningApps_.find(currentForeground_);
    if (fgIt != runningApps_.end()) {
        fgTitle = fgIt->second.windowTitle;
    }

    std::string fgContext = ClassifyContext(currentForeground_, fgTitle);

    // Record hourly activity
    auto sysNow = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(sysNow);
    struct tm tm_buf;
    localtime_s(&tm_buf, &time);
    int currentHour = tm_buf.tm_hour;

    float intervalMin = parallelScanInterval_ / 60.0f;
    int intervalSec = static_cast<int>(parallelScanInterval_);
    ltm_->RecordHourlyActivity(currentForeground_, fgContext, currentHour, intervalMin, 0.0f);

    // InterestGraph: record foreground app as entity event
    if (ig_) {
        std::string sourceHint = "app_usage";
        if (fgContext == "Game") sourceHint = "app_usage_game";
        std::string fgEntityId = ig_->ResolveEntity(currentForeground_, sourceHint);
        if (!fgEntityId.empty()) {
            ig_->RecordEvent(fgEntityId, "used", intervalSec, "engage", sourceHint);
        }
        // Web service gets separate entity
        if (IsBrowser(currentForeground_) && !currentWebService_.empty()) {
            std::string wsSource = "browser";
            std::string wsEntityId = ig_->ResolveEntity(currentWebService_, wsSource);
            if (!wsEntityId.empty()) {
                ig_->RecordEvent(wsEntityId, "viewed", intervalSec, "browse", wsSource);
            }
        }
    }

    for (auto& [name, app] : runningApps_) {
        if (name == currentForeground_) continue;
        if (ShouldIgnore(name)) continue;

        float sessionMin = std::chrono::duration<float>(now - app.openTime).count() / 60.0f;
        if (sessionMin < 1.0f) continue;

        std::string bgContext = ClassifyContext(name, app.windowTitle);

        ltm_->RecordHourlyActivity(name, bgContext, currentHour, 0.0f, intervalMin);

        // InterestGraph: record background app
        if (ig_) {
            std::string bgSource = "app_usage";
            if (bgContext == "Game") bgSource = "app_usage_game";
            else if (bgContext == "Music") bgSource = "browser_music";
            std::string bgEntityId = ig_->ResolveEntity(name, bgSource);
            if (!bgEntityId.empty()) {
                ig_->RecordEvent(bgEntityId, "used", intervalSec, "glance", bgSource);
            }
        }

        if (fgContext == bgContext && !fgContext.empty()) continue;

        ltm_->RecordParallelUsage(currentForeground_, name, fgContext, bgContext, intervalMin);
    }
}
