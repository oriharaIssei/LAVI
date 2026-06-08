#include "GatekeeperManager.h"
#include "SharedMediaContext.h"
#include "LaviContext.h"
#include "EmotionTag.h"
#include "ConversationMemory.h" // 応答をチャット履歴(会話メモリ)へ記録
#include "WebAction.h"
#include "LongTermMemory.h"
#include "KnowledgeBase.h"
#include "WebSearchClient.h"
#include "LocalLLM.h"
#include "system/action/ActionPipeline.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include <chrono>
#include <cstdio>

namespace {
// （Join はキャプチャ評価とともに Screen/MicGateSystem へ移譲したため削除）

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

GatekeeperManager::GatekeeperManager() = default; // GK 本体は Camera/Screen/MicGateSystem が所有（ECS 分解）

GatekeeperManager::~GatekeeperManager() = default;

void GatekeeperManager::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
}

// --- ECS 分解後のアクセサ: GK 本体/結果/寸法は LaviContext(ctx_) 経由で参照する ---
CameraGatekeeper* GatekeeperManager::Camera() { return ctx_ ? ctx_->cameraGate : nullptr; }
ScreenGatekeeper* GatekeeperManager::Screen() { return ctx_ ? ctx_->screenGate : nullptr; }
MicGatekeeper* GatekeeperManager::Mic() { return ctx_ ? ctx_->micGate : nullptr; }
const EmotionResult& GatekeeperManager::CameraResult() const { return ctx_->camResult; }
const ScreenGateResult& GatekeeperManager::ScreenResult() const { return ctx_->screenResult; }
const MicGateResult& GatekeeperManager::MicResult() const { return ctx_->micResult; }
uint32_t GatekeeperManager::CameraFrameWidth() const { return ctx_ ? ctx_->camFrameW : 0; }
uint32_t GatekeeperManager::CameraFrameHeight() const { return ctx_ ? ctx_->camFrameH : 0; }

void GatekeeperManager::IngestPrompt(GateSource src, std::string desc) {
    PushEvent(src, std::move(desc), NowSec()); // 時刻は本クラスのクロックで一貫させる
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

    // 3 つのキャプチャ評価は Camera/Screen/MicGateSystem(Input) が担当し、トリガーを
    // CapturePromptComponent として発行する。GatekeeperSystem がそれを IngestPrompt で
    // 本クラスに取り込む（→ pending_ に蓄積）。ここでは合成ウィンドウ満了の判断のみ行う。
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
        auto& latest = decisions_.back();
        // 行動判断は LLM が [action:...] タグで行う（キーワード事前判定は廃止）。
        Dispatch(latest, now);
    }
}

static bool ContainsSummaryKeyword(const std::string& text) {
    static const char* kw[] = {
        "\xE8\xA6\x81\xE7\xB4\x84", // 要約
        "\xE3\x81\xBE\xE3\x81\xA8\xE3\x82\x81", // まとめ
        "\xE8\xAA\xAC\xE6\x98\x8E", // 説明
        "summary",
    };
    for (const auto* k : kw) {
        if (text.find(k) != std::string::npos) return true;
    }
    return false;
}

void GatekeeperManager::Dispatch(const RouteDecision& dec, double now) {
    if (!ctx_ || llmBusy_) return;

    const bool useLocal = config_.useLocalLLM && localLLM_ && localLLM_->IsModelLoaded(); // localLLM_ は直前に pull 済み
    if (!useLocal && ctx_->config.apiKey.empty()) return; // クラウドは API キー必須

    knowledgeBase_ = LaviContext::Get().knowledgeBase; // KnowledgeSystem が公開した共有を参照
    actionPipeline_ = LaviContext::Get().actionPipeline; // ActionSystem が公開した共有を参照
    localLLM_ = LaviContext::Get().localLLM; // MemoryPanel が公開した共有を参照
    longTermMemory_ = LaviContext::Get().longTermMemory; // MemoryPanel が公開した共有を参照

    std::string systemPrompt = ctx_->config.llmSystemPrompt;

    bool hasMic = false;
    for (auto s : dec.sources) {
        if (s == GateSource::Mic) { hasMic = true; break; }
    }

    if (dec.target == RouteTarget::WebSearch || hasMic) {
        systemPrompt += "\n\n";
        systemPrompt += GetWebActionPrompt();
    }

    // Intent-Driven アクション: LLM が [action:{verb,target,query}] でアプリ/サービスを操作できる。
    if (hasMic && actionPipeline_ && !actionPipeline_->Registry().Empty()) {
        systemPrompt += "\n\n";
        systemPrompt += actionPipeline_->BuildToolPrompt();
    }

    if (longTermMemory_) {
        std::string memCtx = longTermMemory_->BuildCompactContext(ctx_->transcribedText);
        if (!memCtx.empty()) {
            systemPrompt += "\n\n" + memCtx;
        }
    }

    // 知識ベース RAG: 一般知識を関連チャンクとして注入（個人の事実=記憶 と併用）。
    if (knowledgeBase_ && knowledgeBase_->IsReady() && !ctx_->transcribedText.empty()) {
        std::string kbCtx = knowledgeBase_->BuildContext(ctx_->transcribedText);
        if (!kbCtx.empty()) {
            systemPrompt += "\n\n" + kbCtx;
        }
    }

    std::string prompt = dec.prompt;
    if (hasMic && !ctx_->transcribedText.empty()) {
        prompt += "\n\n\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE3\x81\xAE\xE7\x99\xBA\xE8\xA9\xB1: "; // ユーザーの発話:
        prompt += ctx_->transcribedText;
    }

    // 時事対策: 鮮度が要るクエリを検出。ローカルはアプリ側 fetch で文脈注入、
    // クラウドは Claude 内蔵 web_search を有効化する。
    webSearch_ = LaviContext::Get().webSearch; // WebSearchSystem が公開した共有を参照（以降ラムダは this 経由で見る）
    const bool needsFresh = config_.useWebContext && webSearch_ && webSearch_->IsEnabled()
                            && webSearch_->NeedsFreshInfo(ctx_->transcribedText);

    if (useLocal) {
        // ローカル LLM（MemoryPanel の共有インスタンス）で生成。<think> 除去・/no_think は LocalLLM 側で処理。
        std::vector<LocalChatMessage> msgs = { {"user", prompt} };
        const bool toolEnabled = config_.useWebContext && webSearch_ && webSearch_->IsEnabled();
        if (toolEnabled) {
            // モデル主導の tool use: モデルが [search:...] を出したら検索→結果を戻して再生成。
            // Web 取得はブロッキングのため生成ごとバックグラウンドスレッドで実行する。
            const std::string baseSp     = systemPrompt;
            const std::string userPrompt = prompt;
            localFuture_ = std::async(std::launch::async, [this, baseSp, userPrompt]() {
                const std::string sp = baseSp + "\n\n" + WebSearchClient::ToolPrompt();
                std::vector<LocalChatMessage> r1 = { {"user", userPrompt} };
                std::string out = localLLM_->GenerateChatStreamAsync(sp, r1, nullptr).get();

                const std::string q = WebSearchClient::ParseSearchQuery(out);
                if (!q.empty()) {
                    const std::string results = webSearch_->BuildContext(q);
                    const std::string feedback = results.empty()
                        ? std::string("（検索結果は取得できませんでした。手持ちの知識で簡潔に答えてください。）")
                        : (results + "\n上記の検索結果を踏まえて、ユーザーに自然な口調で回答してください。");
                    std::vector<LocalChatMessage> r2 = {
                        { "user", userPrompt },
                        { "assistant", out },
                        { "user", feedback },
                    };
                    out = localLLM_->GenerateChatStreamAsync(sp, r2, nullptr).get();
                }
                return WebSearchClient::StripSearchTags(out);
            });
        } else {
            localFuture_ = localLLM_->GenerateChatStreamAsync(systemPrompt, msgs, nullptr);
        }
        llmBusy_      = true;
        lastEscalate_ = now;
        return;
    }

    // クラウド (Claude)
    llm_.SetApiKey(ctx_->config.apiKey);
    llm_.SetSystemPrompt(systemPrompt);

    bool needSummary = (dec.target == RouteTarget::WebSearch)
                       && ContainsSummaryKeyword(ctx_->transcribedText);
    llm_.SetEnableWebSearch(needSummary || needsFresh);

    llm_.ClearHistory();
    llm_.AddMessage("user", prompt);

    llmFuture_ = llm_.SendAsync();
    llmBusy_ = true;
    lastEscalate_ = now;
}

void GatekeeperManager::PollLlm() {
    if (!llmBusy_) return;

    actionPipeline_ = LaviContext::Get().actionPipeline; // ActionSystem が公開した共有を参照（応答完了時に実行）
    longTermMemory_ = LaviContext::Get().longTermMemory; // MemoryPanel が公開した共有を参照（Webアクションの記憶反映に使用）

    // ローカル LLM の完了
    if (localFuture_.valid()) {
        if (localFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        std::string content = localFuture_.get();
        llmBusy_ = false;
        lastResponse_ = content;
        if (!content.empty()) {
            // チャット履歴(会話メモリ)へ LAVI の応答を記録（タグ類を除去した表示用テキスト）。
            if (ctx_ && ctx_->conversationMemory) {
                ctx_->conversationMemory->PushMessage(
                    "assistant", StripEmotionTag(ActionPipeline::StripActionTags(StripWebActions(content))));
            }
            auto actions = ParseWebActions(content);
            if (!actions.empty()) {
                ExecuteWebActions(actions, longTermMemory_);
            }
            // Intent-Driven アクション（[action:...]）を実行
            if (actionPipeline_) actionPipeline_->ParseAndExecute(content);
            if (config_.autoSpeak) {
                Speak(ActionPipeline::StripActionTags(StripWebActions(content)));
            }
        }
        return;
    }

    // クラウド (Claude) の完了
    if (!llmFuture_.valid()) return;
    if (llmFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    LLMResponse resp = llmFuture_.get();
    llmBusy_ = false;
    lastResponse_ = resp.success ? resp.content : ("[Error] " + resp.error);
    if (resp.success) {
        // チャット履歴(会話メモリ)へ LAVI の応答を記録（タグ類を除去した表示用テキスト）。
        if (ctx_ && ctx_->conversationMemory) {
            ctx_->conversationMemory->PushMessage(
                "assistant", StripEmotionTag(ActionPipeline::StripActionTags(StripWebActions(resp.content))));
        }
        auto actions = ParseWebActions(resp.content);
        if (!actions.empty()) {
            ExecuteWebActions(actions, longTermMemory_);
        }
        if (actionPipeline_) actionPipeline_->ParseAndExecute(resp.content);
        if (config_.autoSpeak) {
            Speak(ActionPipeline::StripActionTags(StripWebActions(resp.content)));
        }
    }
}

void GatekeeperManager::SpeakProactive(const std::string& content) {
    if (!ctx_ || content.empty()) return;

    // 会話メモリへ記録（表示用にタグ除去）。PollLlm の応答完了処理と同じ整形。
    if (ctx_->conversationMemory) {
        ctx_->conversationMemory->PushMessage(
            "assistant", StripEmotionTag(ActionPipeline::StripActionTags(StripWebActions(content))));
    }
    // 発話（autoSpeak 設定に従う）。アクションは実行しない（自発観察の暴発防止）。
    if (config_.autoSpeak) {
        Speak(ActionPipeline::StripActionTags(StripWebActions(content)));
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
    auto& dec = decisions_.back();
    double now = NowSec();

    // 行動判断は LLM が [action:...] タグで行う（キーワード事前判定は廃止）。
    Dispatch(dec, now);
}

void GatekeeperManager::RespondToSpeech() {
    if (!ctx_ || ctx_->transcribedText.empty() || llmBusy_) return;
    double now = NowSec();

    // 発話区間検出で確定した発話なので、キーワード/クールダウンは課さず会話として応答する
    RouteDecision dec;
    dec.target = RouteTarget::Conversation;
    dec.sources = { GateSource::Mic };
    dec.time = now;

    // 行動（アプリ/サービス操作）は LLM が [action:...] タグで判断する。キーワード事前判定は廃止。
    decisions_.push_back(dec);
    if (decisions_.size() > 20) decisions_.erase(decisions_.begin());

    Dispatch(dec, now);
}

static bool ContainsSearchKeyword(const std::string& text) {
    static const char* kw[] = {
        "\xE8\xAA\xBF\xE3\x81\xB9",     // 調べ
        "\xE6\xA4\x9C\xE7\xB4\xA2",     // 検索
        "\xE3\x82\xB5\xE3\x83\xBC\xE3\x83\x81", // サーチ
        "\xE6\xB5\x81\xE3\x81\x97\xE3\x81\xA6", // 流して
        "\xE9\x96\x8B\xE3\x81\x84\xE3\x81\xA6", // 開いて
        "\xE8\xA6\x8B\xE3\x81\x9B\xE3\x81\xA6", // 見せて
        "\xE6\x8E\xA2\xE3\x81\x97\xE3\x81\xA6", // 探して
        "\xE3\x81\x8B\xE3\x81\x91\xE3\x81\xA6", // かけて
        "\xE5\x86\x8D\xE7\x94\x9F",     // 再生
        "\xE8\x81\x9E\xE3\x81\x8B\xE3\x81\x9B\xE3\x81\xA6", // 聞かせて
        "\xE3\x81\xA4\xE3\x81\x91\xE3\x81\xA6", // つけて
        "\xE9\x9F\xB3\xE6\xA5\xBD",     // 音楽
        "\xE5\x8B\x95\xE7\x94\xBB",     // 動画
        "YouTube", "youtube",
        "Spotify", "spotify",
        "search",
    };
    for (const auto* w : kw) {
        if (text.find(w) != std::string::npos) return true;
    }
    return false;
}

RouteTarget GatekeeperManager::Classify(const std::vector<GateEvent>& evs) const {
    bool cam = false, scr = false, mic = false;
    bool hasSearch = false;
    for (const auto& e : evs) {
        if (e.source == GateSource::Camera) cam = true;
        else if (e.source == GateSource::Screen) scr = true;
        else if (e.source == GateSource::Mic) {
            mic = true;
            if (ContainsSearchKeyword(e.description)) hasSearch = true;
        }
    }
    // イベント説明だけでなく、ユーザーの実際の発話も確認
    if (mic && !hasSearch && ctx_ && !ctx_->transcribedText.empty()) {
        hasSearch = ContainsSearchKeyword(ctx_->transcribedText);
    }
    const int distinct = (cam ? 1 : 0) + (scr ? 1 : 0) + (mic ? 1 : 0);
    if (distinct > 1) return RouteTarget::Multi;
    if (mic && hasSearch) return RouteTarget::WebSearch;
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
        case RouteTarget::WebSearch:
            p += "\xE3\x83\xA6\xE3\x83\xBC\xE3\x82\xB6\xE3\x83\xBC\xE3\x81\x8C"  // ユーザーが
                 "Web\xE3\x81\xA7\xE4\xBD\x95\xE3\x81\x8B\xE3\x82\x92\xE3\x81\x97\xE3\x81\x9F\xE3\x81\x84\xE3\x82\x88\xE3\x81\x86\xE3\x81\xA7\xE3\x81\x99\xE3\x80\x82" // Webで何かをしたいようです。
                 "\xE9\x9F\xB3\xE6\xA5\xBD\xE3\x82\x92\xE8\x81\xB4\xE3\x81\x8D\xE3\x81\x9F\xE3\x81\x84\xE3\x83\xBB" // 音楽を聴きたい・
                 "\xE5\x8B\x95\xE7\x94\xBB\xE3\x82\x92\xE8\xA6\x8B\xE3\x81\x9F\xE3\x81\x84\xE3\x83\xBB" // 動画を見たい・
                 "\xE3\x82\xB5\xE3\x82\xA4\xE3\x83\x88\xE3\x82\x92\xE9\x96\x8B\xE3\x81\x8D\xE3\x81\x9F\xE3\x81\x84" // サイトを開きたい
                 "\xE5\xA0\xB4\xE5\x90\x88\xE3\x81\xAF [browser:URL] \xE3\x82\xBF\xE3\x82\xB0\xE3\x81\xA7" // 場合は [browser:URL] タグで
                 "URL\xE3\x82\x92\xE5\x87\xBA\xE5\x8A\x9B\xE3\x81\x97\xE3\x81\xA6\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84\xE3\x80\x82" // URLを出力してください。
                 "\xE8\xA6\x81\xE7\xB4\x84\xE3\x82\x84\xE8\xAA\xBF\xE3\x81\xB9\xE7\x89\xA9\xE3\x81\xAF" // 要約や調べ物は
                 "\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88\xE3\x81\xA7\xE5\x9B\x9E\xE7\xAD\x94\xE3\x81\x97\xE3\x81\xA6\xE3\x81\x8F\xE3\x81\xA0\xE3\x81\x95\xE3\x81\x84\xE3\x80\x82"; // テキストで回答してください。
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
        case RouteTarget::WebSearch:    return "WebSearch";
        case RouteTarget::VisionScreen: return "VisionScreen";
        case RouteTarget::VisionCamera: return "VisionCamera";
        case RouteTarget::Multi:        return "Multi";
        default:                        return "None";
    }
}
