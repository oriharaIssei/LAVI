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
#include <sstream>

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
    if (config_.autoEscalate && phase_ == ReactPhase::Idle && (now - lastEscalate_ >= config_.escalateCooldown)) {
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
    if (!ctx_ || phase_ != ReactPhase::Idle) return;

    knowledgeBase_ = LaviContext::Get().knowledgeBase; // KnowledgeSystem が公開した共有を参照
    actionPipeline_ = LaviContext::Get().actionPipeline; // ActionSystem が公開した共有を参照
    localLLM_ = LaviContext::Get().localLLM; // MemoryPanel が公開した共有を参照
    longTermMemory_ = LaviContext::Get().longTermMemory; // MemoryPanel が公開した共有を参照

    const bool useLocal = config_.useLocalLLM && localLLM_ && localLLM_->IsModelLoaded();
    if (!useLocal && ctx_->config.apiKey.empty()) return; // クラウドは API キー必須

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
    systemPrompt = BuildReactSystemPrompt(systemPrompt);

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
        reactActive_ = true;
        reactUseLocal_ = true;
        reactForceFinal_ = false;
        reactIteration_ = 0;
        // ローカル LLM（MemoryPanel の共有インスタンス）で生成。<think> 除去・/no_think は LocalLLM 側で処理。
        reactMessages_ = { {"user", prompt} };
        reactSystemPrompt_ = systemPrompt;
        // ローカルは [search:] タグでモデル主導の Web 検索を行う。指示を reactSystemPrompt_ に含めることで、
        // 継続ラウンド（observe→re-plan）でも [search:] を出せる。検索の実行は observe ワーカーが担う
        // （独自の 1 回限り検索 async は廃止し、ReAct ループに一本化）。
        const bool toolEnabled = config_.useWebContext && webSearch_ && webSearch_->IsEnabled();
        if (toolEnabled) {
            reactSystemPrompt_ += "\n\n";
            reactSystemPrompt_ += WebSearchClient::ToolPrompt();
        }
        localFuture_  = localLLM_->GenerateChatStreamAsync(reactSystemPrompt_, reactMessages_, nullptr);
        phase_        = ReactPhase::LlmBusy;
        lastEscalate_ = now;
        return;
    }

    // クラウド (Claude)
    reactActive_ = true;
    reactUseLocal_ = false;
    reactForceFinal_ = false;
    reactIteration_ = 0;
    reactMessages_.clear();
    reactSystemPrompt_ = systemPrompt;
    llm_.SetApiKey(ctx_->config.apiKey);
    llm_.SetSystemPrompt(reactSystemPrompt_);

    bool needSummary = (dec.target == RouteTarget::WebSearch)
                       && ContainsSummaryKeyword(ctx_->transcribedText);
    llm_.SetEnableWebSearch(needSummary || needsFresh);

    llm_.ClearHistory();
    llm_.AddMessage("user", prompt);

    llmFuture_ = llm_.SendAsync();
    phase_ = ReactPhase::LlmBusy;
    lastEscalate_ = now;
}

void GatekeeperManager::PollLlm() {
    if (phase_ == ReactPhase::Idle) return;

    // ツール実行(observe)中: ワーカーの結果が出たら再思考 or 確定へ。
    // ここでは共有ポインタを書き換えない（observe ワーカーが読み取り中のため＝データ競合回避）。
    if (phase_ == ReactPhase::ToolBusy) {
        if (!observeFuture_.valid()) { // 安全網（通常は到達しない）
            ResetReactState();
            return;
        }
        if (observeFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        std::string observation;
        try {
            observation = observeFuture_.get();
        } catch (...) {
            // ワーカー側で畳めなかった想定外例外。ターンを安全に終了し Idle へ戻す（無発話）。
            ResetReactState();
            return;
        }
        if (!ContinueReactLoop(reactLastContent_, observation)) {
            FinalizeLlmResponse(reactLastContent_);
        }
        return;
    }

    // ここから phase_ == LlmBusy: LLM 生成の完了回収。
    // observe ワーカーへ渡す共有ポインタは、起動直前のこのタイミングで確定させる（ToolBusy 中は触れない）。
    actionPipeline_ = LaviContext::Get().actionPipeline; // ActionSystem が公開した共有を参照（応答完了時に実行）
    longTermMemory_ = LaviContext::Get().longTermMemory; // MemoryPanel が公開した共有を参照（Webアクションの記憶反映に使用）
    // ローカル LLM の完了
    if (localFuture_.valid()) {
        if (localFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const std::string content = localFuture_.get();
        lastResponse_ = content;
        BeginObserveOrFinalize(content);
        return;
    }

    // クラウド (Claude) の完了
    if (!llmFuture_.valid()) return;
    if (llmFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    LLMResponse resp = llmFuture_.get();
    lastResponse_ = resp.success ? resp.content : ("[Error] " + resp.error);
    if (resp.success) {
        BeginObserveOrFinalize(resp.content);
    } else {
        ResetReactState();
    }
}

bool GatekeeperManager::HasToolTags(const std::string& content) const {
    return content.find("[search:") != std::string::npos
        || content.find("[action:") != std::string::npos
        || content.find("[browser:") != std::string::npos;
}

void GatekeeperManager::BeginObserveOrFinalize(const std::string& content) {
    if (content.empty()) {
        ResetReactState();
        return;
    }
    // 最終確定指示、または行動タグ無し → ツール実行せずに確定（通常会話は 1 ターンで完結）。
    if (reactForceFinal_ || !HasToolTags(content)) {
        FinalizeLlmResponse(content);
        return;
    }
    // ツール実行は BuildContext のブロッキング HTTP を含むためワーカーで実行する。
    // 完了は次フレーム以降の ToolBusy 分岐で回収する（メインスレッドを止めない）。
    reactLastContent_ = content;
    // [browser:] の「サービス既定ブラウザ」解決は LongTermMemory を読むため、ワーカー起動前に
    // メインスレッドで先解決する（AppUsageTracker 等のメイン側書き込みとの競合を回避）。
    const std::vector<ResolvedWebAction> resolvedBrowser =
        ResolveWebActions(ParseWebActions(content), longTermMemory_);
    observeFuture_ = std::async(std::launch::async, [this, content, resolvedBrowser]() -> std::string {
        // 異常系の畳み: 例外を観測テキストに変換し、ReAct が次手で対処できるようにする（クラッシュさせない）。
        try {
            return ExecuteTaggedToolsAndBuildObservation(content, resolvedBrowser);
        } catch (const std::exception& e) {
            return std::string("Tool execution failed: ") + e.what();
        } catch (...) {
            return std::string("Tool execution failed: unknown error");
        }
    });
    phase_ = ReactPhase::ToolBusy;
}

std::string GatekeeperManager::BuildReactSystemPrompt(const std::string& basePrompt) const {
    std::string prompt = basePrompt;
    prompt +=
        "\n\n## ReAct tool loop\n"
        "When you need to use [action:{...}] or [browser:URL], output only the next useful action tags plus any brief natural text.\n"
        "After the app returns a tool observation, re-plan from that observation. If the action failed or produced an empty result, try a better target/query.\n"
        "When you have enough information or the requested action is complete, answer naturally without [action:] or [browser:] tags.\n"
        "Do not repeat the same failing action unless the observation gives a new reason to retry.";

    const std::string decisionCtx = BuildRecentDecisionContext();
    if (!decisionCtx.empty()) {
        prompt += "\n\n" + decisionCtx;
    }
    return prompt;
}

std::string GatekeeperManager::BuildRecentDecisionContext() const {
    if (decisions_.empty()) return "";

    std::ostringstream os;
    os << "## Recent route decisions\n";
    const size_t start = decisions_.size() > 3 ? decisions_.size() - 3 : 0;
    for (size_t i = start; i < decisions_.size(); ++i) {
        const auto& d = decisions_[i];
        os << "- target=" << TargetName(d.target) << ", sources=";
        for (size_t j = 0; j < d.sources.size(); ++j) {
            if (j > 0) os << ",";
            os << SourceName(d.sources[j]);
        }
        os << "\n";
    }
    return os.str();
}

// 注意: この関数は observe ワーカースレッドから呼ばれる（BuildContext のブロッキング HTTP を含むため）。
// ctx_/ECS や LongTermMemory には触れない。ブラウザ操作はメインで先解決済みの resolvedBrowser を使い、
// 検索(webSearch_ は const)とアクション(actionPipeline_ はターン中ワーカー専有)のみ共有ポインタを使う。
// 実行順は 検索 → ブラウザ → アクション（情報取得を先に行ってから副作用を起こす）。
std::string GatekeeperManager::ExecuteTaggedToolsAndBuildObservation(const std::string& content,
                                                                     const std::vector<ResolvedWebAction>& resolvedBrowser) {
    std::ostringstream obs;

    // [search: キーワード] : Web 検索を実行して結果を観測に積む（空振りは別語での再検索を促す）。
    const std::string searchQuery = WebSearchClient::ParseSearchQuery(content);
    if (!searchQuery.empty() && webSearch_ && webSearch_->IsEnabled()) {
        const std::string results = webSearch_->BuildContext(searchQuery); // ブロッキング HTTP（ワーカー上）
        obs << "Web search results for \"" << searchQuery << "\":\n";
        if (results.empty()) {
            obs << "- (no results found; try a different query, or answer from your own knowledge)\n";
        } else {
            obs << results << "\n";
        }
    }

    // [browser:URL] : メインで先解決済み（LTM 非参照）。開く処理のみワーカーで実行する。
    if (!resolvedBrowser.empty()) {
        ExecuteResolvedWebActions(resolvedBrowser);
        obs << "Browser actions executed:\n";
        for (const auto& action : resolvedBrowser) {
            obs << "- opened " << action.url << "\n";
        }
    }

    if (content.find("[action:") != std::string::npos) {
        if (actionPipeline_) {
            const ActionResult result = actionPipeline_->ParseAndExecute(content);
            obs << "Action pipeline result:\n";
            obs << "- handled=" << (result.handled ? "true" : "false") << "\n";
            if (!result.serviceName.empty()) obs << "- service=" << result.serviceName << "\n";
            if (!result.query.empty()) obs << "- query=" << result.query << "\n";
            if (!result.url.empty()) obs << "- url=" << result.url << "\n";
            if (!result.spokenText.empty()) obs << "- spokenText=" << result.spokenText << "\n";
            if (!actionPipeline_->LastPlanSummary().empty()) {
                obs << "- plan=" << actionPipeline_->LastPlanSummary() << "\n";
            }
        } else {
            obs << "Action pipeline result:\n- handled=false\n- error=ActionPipeline is not available\n";
        }
    }

    return obs.str();
}

bool GatekeeperManager::ContinueReactLoop(const std::string& content, const std::string& observation) {
    if (!reactActive_ || observation.empty()) {
        ResetReactState();
        return false;
    }

    reactMessages_.push_back({"assistant", content});
    ++reactIteration_;

    const int maxIterations = config_.maxReactIterations > 0 ? config_.maxReactIterations : 4;
    if (reactIteration_ >= maxIterations) {
        reactMessages_.push_back({
            "user",
            observation + "\nReAct iteration limit reached. Give the user a concise final answer without any [search:], [action:], or [browser:] tags."
        });
        if (reactUseLocal_) {
            if (!localLLM_ || !localLLM_->IsModelLoaded()) {
                ResetReactState();
                return false;
            }
            localFuture_ = localLLM_->GenerateChatStreamAsync(reactSystemPrompt_, reactMessages_, nullptr);
        } else {
            llm_.AddMessage("user", observation + "\nReAct iteration limit reached. Give the user a concise final answer without any [search:], [action:], or [browser:] tags.");
            llmFuture_ = llm_.SendAsync();
        }
        phase_ = ReactPhase::LlmBusy;
        reactForceFinal_ = true;
        return true;
    }

    const std::string feedback =
        observation +
        "\nBased on this observation, either choose the next [search:], [action:], or [browser:] tag, or finish with a natural final response without tool tags.";

    if (reactUseLocal_) {
        if (!localLLM_ || !localLLM_->IsModelLoaded()) {
            ResetReactState();
            return false;
        }
        reactMessages_.push_back({"user", feedback});
        localFuture_ = localLLM_->GenerateChatStreamAsync(reactSystemPrompt_, reactMessages_, nullptr);
    } else {
        llm_.AddMessage("user", feedback);
        llmFuture_ = llm_.SendAsync();
    }

    phase_ = ReactPhase::LlmBusy;
    return true;
}

void GatekeeperManager::FinalizeLlmResponse(const std::string& content) {
    // [search:] は observe で消費するが、最終テキスト（記録・発話）にタグ痕が残らないよう除去する。
    if (ctx_ && ctx_->conversationMemory) {
        ctx_->conversationMemory->PushMessage(
            "assistant",
            StripEmotionTag(ActionPipeline::StripActionTags(StripWebActions(WebSearchClient::StripSearchTags(content)))));
    }
    if (config_.autoSpeak) {
        Speak(ActionPipeline::StripActionTags(StripWebActions(WebSearchClient::StripSearchTags(content))));
    }
    ResetReactState();
}

void GatekeeperManager::ResetReactState() {
    reactActive_ = false;
    reactUseLocal_ = false;
    reactForceFinal_ = false;
    reactIteration_ = 0;
    reactSystemPrompt_.clear();
    reactMessages_.clear();
    phase_ = ReactPhase::Idle; // ループ終了＝アイドルへ（直列化の単一の真実）
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
    if (decisions_.empty() || phase_ != ReactPhase::Idle) return;
    auto& dec = decisions_.back();
    double now = NowSec();

    // 行動判断は LLM が [action:...] タグで行う（キーワード事前判定は廃止）。
    Dispatch(dec, now);
}

void GatekeeperManager::RespondToSpeech() {
    if (!ctx_ || ctx_->transcribedText.empty() || phase_ != ReactPhase::Idle) return;
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
