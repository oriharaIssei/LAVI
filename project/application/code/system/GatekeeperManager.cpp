#include "GatekeeperManager.h"
#include "SharedMediaContext.h"
#include "EmotionTag.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include <chrono>
#include <cstdio>

namespace {
std::string Join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

// 発話用にタグ類 ([joy] [thinking] 等の角括弧表記) を除去する。
// 表示テキストはそのまま、音声に渡すテキストだけクリーンにするために使う。
std::string SanitizeForSpeech(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '[') {
            const size_t close = in.find(']', i);
            if (close != std::string::npos && close - i <= 24) {  // 短い角括弧 = タグ扱い
                i = close + 1;
                continue;
            }
        }
        out += in[i++];
    }
    return out;
}
}  // namespace

GatekeeperManager::GatekeeperManager()
    : camera_(std::make_unique<CameraGatekeeper>()),
      screen_(std::make_unique<ScreenGatekeeper>()),
      mic_(std::make_unique<MicGatekeeper>()) {}

GatekeeperManager::~GatekeeperManager() = default;

void GatekeeperManager::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
}

double GatekeeperManager::NowSec() const {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void GatekeeperManager::PushEvent(GateSource src, std::string desc, double now) {
    GateEvent e{src, std::move(desc), now};
    events_.push_back(e);
    if (events_.size() > 50) events_.erase(events_.begin());

    if (pending_.empty()) pendingStart_ = now;
    pending_.push_back(std::move(e));
}

void GatekeeperManager::Update() {
    if (!ctx_) return;
    const double now = NowSec();

    PollLlm();  // 進行中の Claude 応答を回収・発話

    // --- カメラ (表情変化) ---
    if (config_.camEnabled && camera_->IsReady() && now - lastCam_ >= config_.camInterval) {
        lastCam_ = now;
        if (ctx_->webCamera && ctx_->webCamera->IsCapturing()) {
            uint32_t fw = 0, fh = 0;
            if (ctx_->webCamera->GetLatestFrame(ctx_->camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                lastCamW_ = fw;
                lastCamH_ = fh;
                camResult_ = camera_->Evaluate(ctx_->camFrameBuffer.data(), fw, fh);
                if (camera_->ShouldTrigger(camResult_)) {
                    PushEvent(GateSource::Camera,
                              std::string("表情が ") +
                                  CameraGatekeeper::EmotionName(camResult_.dominant) + " に変化",
                              now);
                }
            }
        }
    }

    // --- 画面 (プロセス変化 / 画面差分) ---
    if (config_.screenEnabled && now - lastScreen_ >= config_.screenInterval) {
        lastScreen_ = now;
        const uint8_t* data = nullptr;
        uint32_t fw = 0, fh = 0;
        if (ctx_->screenCapture && ctx_->screenCapture->IsCapturing()) {
            if (ctx_->screenCapture->GetLatestFrame(ctx_->screenFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
                data = ctx_->screenFrameBuffer.data();
                lastScreenW_ = fw;
                lastScreenH_ = fh;
            } else {
                fw = fh = 0;
            }
        }
        screenResult_ = screen_->Evaluate(data, fw, fh);
        if (screenResult_.triggered) {
            std::string d;
            if (screenResult_.foregroundChanged) d += "アプリ切替: " + screenResult_.foregroundApp;
            if (!screenResult_.newApps.empty()) {
                if (!d.empty()) d += " / ";
                d += "新規起動: " + Join(screenResult_.newApps, ",");
            }
            if (screenResult_.screenChanged) {
                if (!d.empty()) d += " / ";
                char b[32];
                std::snprintf(b, sizeof(b), "画面変化 %.1f%%", screenResult_.screenDiffRatio * 100.0f);
                d += b;
            }
            if (d.empty()) d = "画面イベント";
            PushEvent(GateSource::Screen, d, now);
        }
    }

    // --- マイク (キーワード) ---
    if (config_.micEnabled && now - lastMic_ >= config_.micInterval) {
        lastMic_ = now;
        micResult_ = mic_->Evaluate(ctx_->transcribedText);
        if (micResult_.triggered) {
            PushEvent(GateSource::Mic, "キーワード検知: " + Join(micResult_.matched, ", "), now);
        }
    }

    // --- 合成ウィンドウ満了で 1 件の判断にまとめる ---
    if (!pending_.empty() && (now - pendingStart_ >= config_.combineWindow)) {
        FlushPending(now);
    }
}

void GatekeeperManager::FlushPending(double now) {
    const RouteTarget target = Classify(pending_);

    RouteDecision dec;
    dec.target = target;
    dec.prompt = BuildPrompt(pending_, target);
    dec.time = now;
    // 重複を除いたソース一覧
    for (const auto& e : pending_) {
        bool found = false;
        for (auto s : dec.sources)
            if (s == e.source) { found = true; break; }
        if (!found) dec.sources.push_back(e.source);
    }

    decisions_.push_back(std::move(dec));
    if (decisions_.size() > 20) decisions_.erase(decisions_.begin());

    pending_.clear();

    // 自動エスカレーション
    if (config_.autoEscalate && !llmBusy_ && (now - lastEscalate_ >= config_.escalateCooldown)) {
        Dispatch(decisions_.back(), now);
    }
}

void GatekeeperManager::Dispatch(const RouteDecision& dec, double now) {
    if (!ctx_ || ctx_->config.apiKey.empty() || llmBusy_) return;

    llm_.SetApiKey(ctx_->config.apiKey);
    if (!ctx_->config.llmSystemPrompt.empty()) {
        llm_.SetSystemPrompt(ctx_->config.llmSystemPrompt);
    }
    llm_.ClearHistory();
    llm_.AddMessage("user", dec.prompt);

    llmFuture_ = llm_.SendAsync();
    llmBusy_ = true;
    lastEscalate_ = now;
}

void GatekeeperManager::PollLlm() {
    if (!llmBusy_ || !llmFuture_.valid()) return;
    if (llmFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    LLMResponse resp = llmFuture_.get();
    llmBusy_ = false;
    lastResponse_ = resp.success ? resp.content : ("[Error] " + resp.error);
    if (resp.success && config_.autoSpeak) {
        Speak(resp.content);
    }
}

void GatekeeperManager::Speak(const std::string& text) {
    if (!ctx_ || !ctx_->voiceVox || !ctx_->voiceVox->IsEngineReady()) return;
    if (ctx_->voiceVoxSpeakers.empty() || ctx_->isSpeaking) return;

    // 読み上げは "..." で囲まれた部分のみ。クオートが無ければ旧来のタグ除去でフォールバック。
    std::string spoken = ExtractSpokenText(text);
    if (spoken.empty()) spoken = SanitizeForSpeech(text);
    if (spoken.empty()) return;

    int idx = ctx_->selectedSpeaker;
    if (idx < 0 || idx >= static_cast<int>(ctx_->voiceVoxSpeakers.size())) idx = 0;
    const int speakerId = ctx_->voiceVoxSpeakers[idx].id;

    ctx_->speakFuture = ctx_->voiceVox->SpeakAsync(spoken, speakerId);
    ctx_->isSpeaking = true;
}

void GatekeeperManager::EscalateLatest() {
    if (decisions_.empty() || llmBusy_) return;
    Dispatch(decisions_.back(), NowSec());
}

RouteTarget GatekeeperManager::Classify(const std::vector<GateEvent>& evs) {
    bool cam = false, scr = false, mic = false;
    for (const auto& e : evs) {
        if (e.source == GateSource::Camera) cam = true;
        else if (e.source == GateSource::Screen) scr = true;
        else if (e.source == GateSource::Mic) mic = true;
    }
    const int distinct = (cam ? 1 : 0) + (scr ? 1 : 0) + (mic ? 1 : 0);
    if (distinct > 1) return RouteTarget::Multi;
    if (mic) return RouteTarget::Conversation;
    if (scr) return RouteTarget::VisionScreen;
    if (cam) return RouteTarget::VisionCamera;
    return RouteTarget::None;
}

std::string GatekeeperManager::BuildPrompt(const std::vector<GateEvent>& evs, RouteTarget t) {
    std::string p = "以下の状況が検知されました:\n";
    for (const auto& e : evs) {
        p += "- [";
        p += SourceName(e.source);
        p += "] " + e.description + "\n";
    }
    switch (t) {
        case RouteTarget::Conversation:
            p += "ユーザーが話しかけています。自然に応答してください。";
            break;
        case RouteTarget::VisionScreen:
            p += "画面に変化がありました。状況を踏まえて短く一言コメントしてください。";
            break;
        case RouteTarget::VisionCamera:
            p += "ユーザーの表情が変化しました。気遣う一言をかけてください。";
            break;
        case RouteTarget::Multi:
            p += "複数の変化が同時に起きています。総合的に判断して適切に反応してください。";
            break;
        default:
            break;
    }
    return p;
}

void GatekeeperManager::ClearLog() {
    events_.clear();
    decisions_.clear();
    pending_.clear();
}

const char* GatekeeperManager::SourceName(GateSource s) {
    switch (s) {
        case GateSource::Camera: return "Camera";
        case GateSource::Screen: return "Screen";
        case GateSource::Mic:    return "Mic";
        default:                 return "?";
    }
}

const char* GatekeeperManager::TargetName(RouteTarget t) {
    switch (t) {
        case RouteTarget::Conversation: return "Conversation";
        case RouteTarget::VisionScreen: return "VisionScreen";
        case RouteTarget::VisionCamera: return "VisionCamera";
        case RouteTarget::Multi:        return "Multi";
        default:                        return "None";
    }
}
