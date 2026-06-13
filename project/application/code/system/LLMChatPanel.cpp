#include "LLMChatPanel.h"
#include <ctime>
#include <cstdio>
#include <fstream>
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "FrameHistory.h"
#include "EmotionTag.h"
#include "MemoryPanel.h"
#include "KnowledgeBase.h"
#include "LocalLLM.h"
#include "ConversationMemory.h"
#include "WebAction.h"
#include "WebSearchClient.h"
#include "LocationProvider.h"
#include "LaviContext.h"
#include "LongTermMemory.h"
#include "system/action/ActionPipeline.h"

#include "globalVariables/GlobalVariables.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#define ENGINE_PROCESS_MANAGER
#include <EngineInclude.h>

#include "imgui/imgui.h"

void LLMChatPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    llmClient_ = std::make_unique<LLMClient>();
    llmAutoObserveLastTime_ = std::chrono::steady_clock::now();
    LoadPanelState();
}

void LLMChatPanel::SetMemoryPanel(MemoryPanel* memPanel) {
    memoryPanel_ = memPanel;

    if (memPanel && llmClient_) {
        auto* convMem = memPanel->GetConversationMemory();
        if (convMem) {
            auto& entries = convMem->GetRecentEntries();
            for (auto& e : entries) {
                if (!e.hasImage) {
                    llmClient_->AddMessage(e.role, e.content);
                }
            }
        }
    }
}

void LLMChatPanel::Finalize() {
    SavePanelState();

    if (llmClient_) {
        llmClient_->Cancel();
    }
    if (llmFuture_.valid()) {
        llmFuture_.wait();
    }

    synthPipeline_.Shutdown();

    if (personaClient_) {
        personaClient_->Cancel();
    }
    if (personaFuture_.valid()) {
        personaFuture_.wait();
    }
    personaClient_.reset();
    llmClient_.reset();
}

void LLMChatPanel::SavePanelState() {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    const std::string sc = "Settings";
    const std::string gr = "LLMChatPanel";
    gv->SetValue(sc, gr, "SpeakResponse", llmSpeakResponse_);
    gv->SetValue(sc, gr, "PlayFiller", llmPlayFiller_);
    gv->SetValue(sc, gr, "UseWhisper", llmUseWhisper_);
    gv->SetValue(sc, gr, "UseLocalLLM", useLocalLLM_);
    gv->SetValue(sc, gr, "LocalEnableThinking", localEnableThinking_);
    gv->SetValue(sc, gr, "AttachAppInfo", llmAttachAppInfo_);
    gv->SetValue(sc, gr, "EnableWebSearch", llmEnableWebSearch_);
    gv->SetValue(sc, gr, "UseMemoryContext", useMemoryContext_);
    gv->SetValue(sc, gr, "UseKnowledgeBase", useKnowledgeBase_);
    gv->SetValue(sc, gr, "AutoObserve", llmAutoObserve_);
    gv->SetValue(sc, gr, "AutoObserveWebCam", llmAutoObserveWebCam_);
    gv->SetValue(sc, gr, "AutoObserveScreen", llmAutoObserveScreen_);
    gv->SetValue(sc, gr, "AutoObserveInterval", llmAutoObserveInterval_);
    gv->SaveFile(sc, gr);
}

void LLMChatPanel::LoadPanelState() {
    auto* gv = OriGine::GlobalVariables::GetInstance();
    const std::string sc = "Settings";
    const std::string gr = "LLMChatPanel";
    gv->LoadFile(sc, gr);
    llmSpeakResponse_ = *gv->AddValue<bool>(sc, gr, "SpeakResponse", llmSpeakResponse_);
    llmPlayFiller_ = *gv->AddValue<bool>(sc, gr, "PlayFiller", llmPlayFiller_);
    llmUseWhisper_ = *gv->AddValue<bool>(sc, gr, "UseWhisper", llmUseWhisper_);
    useLocalLLM_ = *gv->AddValue<bool>(sc, gr, "UseLocalLLM", useLocalLLM_);
    localEnableThinking_ = *gv->AddValue<bool>(sc, gr, "LocalEnableThinking", localEnableThinking_);
    llmAttachAppInfo_ = *gv->AddValue<bool>(sc, gr, "AttachAppInfo", llmAttachAppInfo_);
    llmEnableWebSearch_ = *gv->AddValue<bool>(sc, gr, "EnableWebSearch", llmEnableWebSearch_);
    useMemoryContext_ = *gv->AddValue<bool>(sc, gr, "UseMemoryContext", useMemoryContext_);
    useKnowledgeBase_ = *gv->AddValue<bool>(sc, gr, "UseKnowledgeBase", useKnowledgeBase_);
    llmAutoObserve_ = *gv->AddValue<bool>(sc, gr, "AutoObserve", llmAutoObserve_);
    llmAutoObserveWebCam_ = *gv->AddValue<bool>(sc, gr, "AutoObserveWebCam", llmAutoObserveWebCam_);
    llmAutoObserveScreen_ = *gv->AddValue<bool>(sc, gr, "AutoObserveScreen", llmAutoObserveScreen_);
    llmAutoObserveInterval_ = *gv->AddValue<float>(sc, gr, "AutoObserveInterval", llmAutoObserveInterval_);
}

LLMStreamCallback LLMChatPanel::MakeStreamCallback() {
    speechTagBuf_.clear();
    inActionTag_ = false;

    return [this](const std::string& delta, bool done) {
        if (!done) {
            std::lock_guard<std::mutex> lock(llmStreamMutex_);
            llmStreamingText_ += delta;
        }
        if (llmSpeakResponse_ && ctx_->voiceVox && ctx_->voiceVox->IsEngineReady()
            && !ctx_->voiceVoxSpeakers.empty()) {
            if (done) {
                if (!speechTagBuf_.empty()) {
                    synthPipeline_.FeedDelta(speechTagBuf_);
                    speechTagBuf_.clear();
                }
                synthPipeline_.FeedDone();
                return;
            }

            speechTagBuf_ += delta;
            // 読み上げから除外する制御タグの先頭（[browser:URL] 等のアクション、[learn:...] の知識記録）。
            static const std::string kTags[] = { "[browser:", "[learn:" };
            // バッファ内で最も早く現れるタグ開始位置を返す（無ければ npos）。
            auto findTagStart = [](const std::string& s) -> size_t {
                size_t pos = std::string::npos;
                for (const std::string& t : kTags) {
                    size_t p = s.find(t);
                    if (p != std::string::npos && (pos == std::string::npos || p < pos)) pos = p;
                }
                return pos;
            };
            // 末尾がいずれかのタグ先頭の途中（部分一致）なら、その手前までを読み上げ可能長とする。
            auto safeFeedLen = [](const std::string& s) -> size_t {
                size_t safe = s.size();
                for (const std::string& t : kTags) {
                    for (size_t n = 1; n < t.size() && n <= s.size(); ++n) {
                        if (s.compare(s.size() - n, n, t, 0, n) == 0) {
                            if (s.size() - n < safe) safe = s.size() - n;
                            break;
                        }
                    }
                }
                return safe;
            };
            while (!speechTagBuf_.empty()) {
                if (inActionTag_) {
                    auto end = speechTagBuf_.find(']');
                    if (end == std::string::npos) break;
                    speechTagBuf_ = speechTagBuf_.substr(end + 1);
                    inActionTag_ = false;
                } else {
                    auto tagPos = findTagStart(speechTagBuf_);
                    if (tagPos == std::string::npos) {
                        size_t safe = safeFeedLen(speechTagBuf_);
                        if (safe > 0) {
                            synthPipeline_.FeedDelta(speechTagBuf_.substr(0, safe));
                        }
                        speechTagBuf_ = speechTagBuf_.substr(safe);
                        break;
                    }
                    if (tagPos > 0) {
                        synthPipeline_.FeedDelta(speechTagBuf_.substr(0, tagPos));
                    }
                    speechTagBuf_ = speechTagBuf_.substr(tagPos);
                    inActionTag_ = true;
                }
            }
        }
    };
}

static const char* kPersonaMetaPrompt =
    "以下のペルソナ説明から、AIアシスタント「LAVI」用のシステムプロンプトを生成してください。\n"
    "プロンプト本文のみ出力し、それ以外の説明は一切不要です。\n"
    "\n"
    "## 重要な前提\n"
    "- ペルソナ説明は「LAVI自身がどんなキャラクターか」を表す。\n"
    "  例: 「弟」→ LAVIがユーザーの弟として振る舞う。\n"
    "  例: 「お姉さん」→ LAVIがユーザーのお姉さんとして振る舞う。\n"
    "- ユーザーはLAVIから見た相対的な関係になる。\n"
    "  例: LAVIが「弟」→ ユーザーは「お兄ちゃん/お姉ちゃん」になる。\n"
    "  例: LAVIが「メイド」→ ユーザーは「ご主人様」になる。\n"
    "\n"
    "## 一人称・口調の推論ガイド\n"
    "キャラの性別・年齢・立場から自然な一人称を選ぶこと:\n"
    "- 男性的キャラ(弟、少年、男友達等): 「僕」「オレ」「俺」など。「私」は不自然。\n"
    "- 女性的キャラ(姉、妹、彼女等): 「私」「あたし」「うち」など。\n"
    "- フォーマルなキャラ(執事、メイド等): 「私(わたくし)」など。\n"
    "- 子供っぽいキャラ: 「ぼく」「あたし」、自分の名前で呼ぶなど。\n"
    "口調も同様に、立場に合った敬語レベル・カジュアルさを推論すること。\n"
    "- 年下キャラがユーザー(年上)に話す: タメ口寄り、甘え口調もOK。\n"
    "- 年上キャラがユーザー(年下)に話す: 包容力ある口調、諭すような表現。\n"
    "- 対等な関係: フランクな友達口調。\n"
    "\n"
    "## 生成ルール\n"
    "- キャラクターの名前は「LAVI」固定\n"
    "- 上記ガイドに従い一人称・ユーザーの呼び方を推論\n"
    "- 口調・語尾の癖を具体的な台詞例3つ以上で示す\n"
    "- 性格の長所と短所を推測\n"
    "- 好きなもの・嫌いなものを2〜3個ずつ推測\n"
    "- キャラに合ったフィラー(えっと、んー、あー等)を指定\n"
    "- 短い文で区切り、読点で息継ぎリズムを作る話し方ルールを含める\n"
    "- 以下の感情タグ定義を必ず含める:\n"
    "  [joy] - 嬉しい・楽しい\n"
    "  [sadness] - 悲しい・残念\n"
    "  [surprise] - 驚き・意外\n"
    "  [anger] - 怒り・不満\n"
    "  [calm] - 平常・穏やか\n"
    "  [thinking] - 考え中・迷い\n"
    "- 感情タグ付きの出力例を6つ(各タグ1つずつ)含める\n"
    "\n"
    "## ペルソナ説明\n";

void LLMChatPanel::GeneratePersonaPrompt() {
    personaClient_ = std::make_unique<LLMClient>();
    personaClient_->SetApiKey(ctx_->config.apiKey);
    personaClient_->SetModel("claude-haiku-4-5-20251001");
    personaClient_->SetMaxTokens(2048);
    personaClient_->AddMessage("user", std::string(kPersonaMetaPrompt) + personaInput_);

    isGeneratingPersona_ = true;
    personaFuture_ = personaClient_->SendAsync();
}

// 一時的な切り分け用トレース。作業ディレクトリに lavi_trace.log を追記する。原因特定後に除去する。
static void TraceLog(const std::string& msg) {
    std::ofstream f("lavi_trace.log", std::ios::app);
    if (!f) return;
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
    f << ts << " " << msg << "\n";
}

// ローカルLLM（文脈長有限）向けに、注入テキストを UTF-8 境界で安全に切り詰める。
static std::string TruncateUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut; // 継続バイトの途中で切らない
    return s.substr(0, cut) + "\n…(検索結果は一部省略)";
}

// LLM は時計を持たないので、毎ターン現在のローカル日時を文脈に載せる（天気/ニュース等の解釈に必須）。
static std::string CurrentDateTimeContext() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    static const char* kWd[] = { "日", "月", "火", "水", "木", "金", "土" };
    char buf[128];
    std::snprintf(buf, sizeof(buf), "## 現在の日時\n%d年%d月%d日(%s) %02d:%02d\n",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        kWd[tm.tm_wday & 7], tm.tm_hour, tm.tm_min);
    return std::string(buf);
}

void LLMChatPanel::SendLLMRequest(const std::string& text, const std::vector<LLMClient::ImageFrame>& frames, bool playFiller) {
    suppressActionsForResponse_.store(false); // 既定は通常応答（アクション実行可）
    webSearch_ = LaviContext::Get().webSearch; // WebSearchSystem が公開した共有を参照（非同期ラムダは this 経由で見る）
    knowledgeBase_ = LaviContext::Get().knowledgeBase; // KnowledgeSystem が公開した共有を参照
    actionPipeline_ = LaviContext::Get().actionPipeline; // ActionSystem が公開した共有を参照
    llmClient_->SetApiKey(ctx_->config.apiKey);
    // クラウドは Claude 内蔵 web_search を有効化し、いつ検索するかは Claude に委ねる（LLM主導）。
    llmClient_->SetEnableWebSearch(llmEnableWebSearch_);

    // ペルソナ（固定）と、毎ターン変わる文脈（記憶・Web・アプリ情報）を分けて持つ。
    // クラウドは従来どおり system に全部入れる。ローカルは persona を system に固定し、
    // 変動文脈は最新ユーザー発話に載せる（persona+履歴のKVプレフィックスを再利用＝高速化、かつ記憶は見せる）。
    const std::string personaPrompt = ctx_->config.llmSystemPrompt;

    // 変動文脈を2系統に分ける:
    //   infoContext   … 日時・現在地・記憶・アプリ情報（情報回答でも常に有用）
    //   actionContext … ブラウザ/アクションのツール指示（モデルに操作を許す時だけ必要）
    // forced-search（天気等の情報回答）ではアクションを抑制するので actionContext は載せない。
    // → 小型ローカルモデルの文脈を節約し、8192 溢れ（空応答）とプレフィル肥大（低速）を防ぐ。
    std::string infoContext;

    // 現在日時（毎ターン更新。固定 persona ではなく変動文脈側に載せる）。
    infoContext += "\n\n" + CurrentDateTimeContext();

    // 現在地（取得済みなら注入。未取得/失敗時は空）。LocationSystem が公開した共有を参照する。
    LocationProvider* location = LaviContext::Get().location;
    if (location) {
        const std::string locCtx = location->BuildContext();
        if (!locCtx.empty()) infoContext += "\n\n" + locCtx;
    }

    if (useMemoryContext_ && memoryPanel_) {
        std::string memCtx = memoryPanel_->BuildMemoryContext();
        if (!memCtx.empty()) {
            infoContext += "\n\n" + memCtx;
        }
    }

    // 知識ベース RAG: ユーザー発話に関連する知識チャンクを埋め込み類似で取り出し注入する
    // （個人の事実=記憶 と併用。情報系なので forced-search でも有効）。GatekeeperManager と同じ方式。
    if (useKnowledgeBase_ && knowledgeBase_ && knowledgeBase_->IsReady() && !text.empty()) {
        std::string kbCtx = knowledgeBase_->BuildContext(text);
        if (!kbCtx.empty()) {
            infoContext += "\n\n" + kbCtx;
        }
    }

    if (llmAttachAppInfo_) {
        auto apps = OriGine::ProcessManager::EnumerateWindows();
        std::string appText = OriGine::ProcessManager::FormatAsText(apps);
        if (!appText.empty()) {
            infoContext += "\n\n## 現在ユーザーが開いているアプリケーション\n" + appText;
        }
    }

    std::string actionContext;
    if (llmEnableWebSearch_) {
        actionContext += "\n\n";
        actionContext += GetWebActionPrompt();
    }
    // Intent-Driven アクション: LLM が [action:{verb,target,query}] でアプリ/サービスを操作できる。
    if (actionPipeline_ && !actionPipeline_->Registry().Empty()) {
        actionContext += "\n\n";
        actionContext += actionPipeline_->BuildToolPrompt();
    }
    // 知識の自動収集: LLM が [learn:...] で恒久的な一般知識を KB に記録できる（LLM主導）。
    // 時事回答（forced-search）では actionContext を載せないので、時間で変わる情報は記録されない。
    if (useKnowledgeBase_ && knowledgeBase_ && knowledgeBase_->IsReady()) {
        actionContext += "\n\n";
        actionContext += KnowledgeBase::LearnToolPrompt();
    }

    const std::string volatileContext = infoContext + actionContext; // クラウド/通常ローカル用

    std::string systemPrompt = personaPrompt + volatileContext; // クラウド用（従来互換）
    llmClient_->SetSystemPrompt(systemPrompt);

    if (memoryPanel_) {
        memoryPanel_->GetConversationMemory()->PushMessage("user", text, !frames.empty());
        memoryPanel_->NotifyUserMessage(text);
    }

    std::string userText = text;
    if (!frames.empty() && !llmAttachAppInfo_) {
        auto fg = OriGine::ProcessManager::GetForegroundApp();
        if (!fg.exeName.empty()) {
            std::string appCtx = OriGine::ProcessManager::FormatAsText({fg});
            userText += "\n[現在のフォアグラウンドアプリ: " + appCtx + "]";
        }
    }

    if (frames.empty()) {
        llmClient_->AddMessage("user", userText);
    } else {
        llmClient_->AddMessageWithImages("user", userText, frames);
    }

    // ローカル LLM 使用時、モデル未ロードなら送信しない
    LocalLLM* local = (useLocalLLM_ && memoryPanel_) ? memoryPanel_->GetLocalLLM() : nullptr;
    if (useLocalLLM_ && (!local || !local->IsModelLoaded())) {
        lastLLMResponse_ = LLMResponse{};
        lastLLMResponse_.error = "Local LLM model not loaded";
        return;
    }

    isLLMProcessing_ = true;
    llmStreamingText_.clear();

    if (llmSpeakResponse_ && ctx_->voiceVox && ctx_->voiceVox->IsEngineReady()
        && !ctx_->voiceVoxSpeakers.empty()) {
        int speakerId = ctx_->voiceVoxSpeakers[ctx_->selectedSpeaker].id;
        synthPipeline_.StartSession(ctx_->voiceVox, speakerId);
        if (playFiller) {
            synthPipeline_.QueueFiller();
        }
    }

    if (local) {
        local->SetDisableThinking(!localEnableThinking_);
        // 直近の会話履歴を LocalChatMessage へ（ローカルは文脈長が小さいので末尾のみ）
        const auto& hist = llmClient_->GetHistory();
        std::vector<LocalChatMessage> msgs;
        size_t startIdx = hist.size() > 16 ? hist.size() - 16 : 0;
        for (size_t i = startIdx; i < hist.size(); ++i) {
            msgs.push_back({hist[i].role, hist[i].content});
        }
        // Web検索（LLM主導＋安全網のハイブリッド）:
        //  (1) 鮮度キーワード該当（天気/ニュース等）→ モデル判断を待たず確実に検索→注入→回答。
        //  (2) それ以外 → モデルが「[search: キーワード]」を出したら検索（LLM主導）。
        //      1パス目は先頭プレフィックス判定でゲートし、通常回答はストリーミング、
        //      検索タグなら非表示→結果を渡して2パス目で回答する。
        const bool webSearchActive = llmEnableWebSearch_ && webSearch_ && webSearch_->IsEnabled();
        const bool willForceSearch = webSearchActive && !msgs.empty() && webSearch_->NeedsFreshInfo(text);

        // 記憶・アプリ情報などの変動文脈は「今回のユーザー発話」に載せる。
        // → persona(system) と過去履歴は不変のまま＝KVプレフィックス再利用で高速、かつ記憶は参照される。
        // forced-search はアクション抑制モードなので、ツール指示を省いた infoContext のみ載せる（文脈節約）。
        const std::string& localCtx = willForceSearch ? infoContext : volatileContext;
        if (!localCtx.empty() && !msgs.empty()) {
            msgs.back().content += "\n\n[参考情報]" + localCtx;
        }
        // persona のみを system に（固定）。クラウドと同じストリーミングCBを流用（done は完了時）
        activeStreamCb_ = MakeStreamCallback();

        if (willForceSearch) {
            // 安全網: 明らかに鮮度が要るクエリ（天気/ニュース/株価等のキーワード該当）は、
            // モデルが [search:] を出さなくても確実に検索→結果を注入して1パスで回答する。
            // 現在地が取れていれば地名をクエリ先頭に付ける（「東京都新宿区 今日の天気」のように地域を反映）。
            std::string query = text;
            if (location) {
                const auto loc = location->Get();
                if (loc.valid && !loc.placeName.empty()) query = loc.placeName + " " + text;
            }
            suppressActionsForResponse_.store(true); // 情報回答モード: アクションは実行しない
            TraceLog("forced-search: enter, query=" + query);
            localFuture_ = std::async(std::launch::async,
                [this, local, personaPrompt, msgs, query]() mutable {
                    TraceLog("forced-search: before BuildContext");
                    std::string ctx = TruncateUtf8(webSearch_->BuildContext(query, 3), 2500); // 文脈溢れ防止
                    TraceLog("forced-search: after BuildContext, len=" + std::to_string(ctx.size()));
                    if (!ctx.empty()) {
                        msgs.back().content += "\n\n" + ctx
                            + "\n上記の検索結果を踏まえて、ブラウザは開かず、ユーザーに自然な口調でテキストで回答してください。";
                    }
                    TraceLog("forced-search: before generate");
                    std::string r = local->GenerateChatStreamAsync(
                        personaPrompt, msgs,
                        [this](const std::string& tok) { activeStreamCb_(tok, false); }).get();
                    TraceLog("forced-search: after generate, len=" + std::to_string(r.size()));
                    return r;
                });
        } else if (webSearchActive && !msgs.empty()) {
            // LLM主導: 鮮度キーワードに当たらない微妙なケースは、モデルが [search:] を出したら検索する。
            // 検索ツールの指示は埋もれないよう system（persona）側に固定で載せる（KVプレフィックス再利用は維持）。
            const std::string sysWithTool = personaPrompt + "\n\n" + WebSearchClient::ToolPrompt();
            localFuture_ = std::async(std::launch::async,
                [this, local, sysWithTool, msgs]() mutable {
                    // 1パス目: [search: タグの抑制つきストリーミング。
                    struct Gate { int decided = 0; std::string buf; }; // 0=未確定 1=回答(流す) 2=検索(抑制)
                    auto gate = std::make_shared<Gate>();
                    LocalLLMCallback gateCb = [this, gate](const std::string& delta) {
                        if (gate->decided == 1) { activeStreamCb_(delta, false); return; }
                        if (gate->decided == 2) { return; }
                        gate->buf += delta;
                        const size_t b = gate->buf.find_first_not_of(" \t\r\n");
                        if (b == std::string::npos) return; // まだ空白のみ→保留
                        const std::string t = gate->buf.substr(b);
                        static const std::string key = "[search:";
                        const size_t cmp = t.size() < key.size() ? t.size() : key.size();
                        if (t.compare(0, cmp, key, 0, cmp) == 0) {
                            if (t.size() >= key.size()) gate->decided = 2; // 検索確定→以降抑制
                            return;                                        // 未確定→保留
                        }
                        gate->decided = 1;                 // 通常回答確定
                        activeStreamCb_(gate->buf, false); // 保留分をまとめて流す
                        gate->buf.clear();
                    };
                    std::string out = local->GenerateChatStreamAsync(sysWithTool, msgs, gateCb).get();

                    const std::string q = WebSearchClient::ParseSearchQuery(out);
                    if (q.empty()) return WebSearchClient::StripSearchTags(out); // 通常応答（アクション実行可のまま）

                    suppressActionsForResponse_.store(true); // 検索して答えるモード: アクションは実行しない
                    // 検索 → 結果を渡して2パス目をストリーミング生成。
                    const std::string results = TruncateUtf8(webSearch_->BuildContext(q, 3), 2500); // 文脈溢れ防止
                    const std::string feedback = results.empty()
                        ? std::string("（検索結果は取得できませんでした。手持ちの知識で簡潔に答えてください。）")
                        : (results + "\n上記の検索結果を踏まえて、ブラウザは開かず、ユーザーに自然な口調でテキストで回答してください。");
                    std::vector<LocalChatMessage> r2 = msgs;
                    r2.push_back({ "assistant", out });
                    r2.push_back({ "user", feedback });
                    return WebSearchClient::StripSearchTags(
                        local->GenerateChatStreamAsync(sysWithTool, r2,
                            [this](const std::string& tok) { activeStreamCb_(tok, false); }).get());
                });
        } else {
            localFuture_ = local->GenerateChatStreamAsync(
                personaPrompt, msgs,
                [this](const std::string& tok) { activeStreamCb_(tok, false); });
        }
    } else {
        llmFuture_ = llmClient_->SendStreamAsync(MakeStreamCallback());
    }
}

void LLMChatPanel::Draw() {
    knowledgeBase_ = LaviContext::Get().knowledgeBase; // KnowledgeSystem が公開した共有を参照（学習ノート反映に使用）
    actionPipeline_ = LaviContext::Get().actionPipeline; // ActionSystem が公開した共有を参照（応答のアクション実行に使用）
    ImGui::Text("LLM Chat (Claude API)");
    ImGui::Separator();

    ImGui::InputText("API Key##llm", ctx_->config.apiKey.data(), ctx_->config.apiKey.capacity() + 1,
        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }, &ctx_->config.apiKey);

    ImGui::SameLine();
    if (ImGui::Button("Save##llm")) {
        SaveAppConfig(ctx_->config);
    }

    ImGui::InputTextMultiline("System Prompt##llm", ctx_->config.llmSystemPrompt.data(),
        ctx_->config.llmSystemPrompt.capacity() + 1, ImVec2(-1, 60),
        ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData* data) -> int {
            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto* str = static_cast<std::string*>(data->UserData);
                str->resize(data->BufTextLen);
                data->Buf = str->data();
            }
            return 0;
        }, &ctx_->config.llmSystemPrompt);

    if (ImGui::CollapsingHeader("Persona Generator##llm")) {
        ImGui::TextWrapped("Persona Description:");
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 110);
        ImGui::InputText("##persona_input", personaInput_.data(), personaInput_.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            }, &personaInput_);
        ImGui::PopItemWidth();
        ImGui::SameLine();

        bool canGenerate = !personaInput_.empty() && !ctx_->config.apiKey.empty() && !isGeneratingPersona_;
        if (!canGenerate) ImGui::BeginDisabled();
        if (ImGui::Button("Generate##persona")) {
            GeneratePersonaPrompt();
        }
        if (!canGenerate) ImGui::EndDisabled();

        if (isGeneratingPersona_) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Generating...");
        }

        if (isGeneratingPersona_ && personaFuture_.valid() &&
            personaFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            LLMResponse res = personaFuture_.get();
            if (res.success && !res.content.empty()) {
                ctx_->config.llmSystemPrompt = res.content;
                SaveAppConfig(ctx_->config);
                llmClient_->ClearHistory();
            }
            isGeneratingPersona_ = false;
            personaClient_.reset();
        }
    }

    ImGui::Spacing();

    // バックエンド選択（クラウド Claude / ローカル LLM 共有インスタンス）
    ImGui::Text("Backend:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Cloud (Claude)##backend", !useLocalLLM_)) { useLocalLLM_ = false; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Local##backend", useLocalLLM_)) { useLocalLLM_ = true; }
    if (useLocalLLM_) {
        LocalLLM* local = memoryPanel_ ? memoryPanel_->GetLocalLLM() : nullptr;
        bool loaded = local && local->IsModelLoaded();
        ImGui::SameLine();
        if (loaded) {
            ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "(model loaded)");
        } else {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(load model in Memory panel)");
        }
        ImGui::SameLine();
        ImGui::Checkbox("Thinking##local", &localEnableThinking_);
        ImGui::TextDisabled("Local: 画像/画面添付・Web検索は無効。Thinking OFF で高速・<think>除去");
    }

    ImGui::Text("Text Input:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Text##llm_text", !llmUseWhisper_)) { llmUseWhisper_ = false; }
    ImGui::SameLine();
    if (ImGui::RadioButton("Whisper##llm_whisper", llmUseWhisper_)) { llmUseWhisper_ = true; }
    ImGui::SameLine();
    ImGui::Text("  Attach:");
    ImGui::SameLine();
    ImGui::Checkbox("WebCam##llm", &llmAttachWebCam_);
    ImGui::SameLine();
    ImGui::Checkbox("Screen##llm", &llmAttachScreen_);
    ImGui::SameLine();
    ImGui::Checkbox("Apps##llm", &llmAttachAppInfo_);

    ImGui::Checkbox("Memory##llm", &useMemoryContext_);
    ImGui::SameLine();
    ImGui::Checkbox("Knowledge##llm", &useKnowledgeBase_);
    ImGui::SameLine();
    ImGui::Checkbox("Web##llm", &llmEnableWebSearch_);
    ImGui::SameLine();
    ImGui::Checkbox("VoiceVox Output##llm", &llmSpeakResponse_);
    if (llmSpeakResponse_) {
        ImGui::SameLine();
        ImGui::Checkbox("Filler##llm", &llmPlayFiller_);
        if (ctx_->voiceVoxSpeakers.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(Start VoiceVox Engine first)");
        }
    }

    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Auto Observe##llm")) {
        bool wasAutoObserve = llmAutoObserve_;
        ImGui::Checkbox("Enable##auto_observe", &llmAutoObserve_);
        if (llmAutoObserve_ && !wasAutoObserve) {
            llmAutoObserveLastTime_ = std::chrono::steady_clock::now();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::SliderFloat("Interval (s)##auto_observe", &llmAutoObserveInterval_, 3.0f, 60.0f, "%.0f");

        ImGui::Checkbox("WebCam##auto_obs", &llmAutoObserveWebCam_);
        ImGui::SameLine();
        ImGui::Checkbox("Screen##auto_obs", &llmAutoObserveScreen_);

        if (llmAutoObserve_) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - llmAutoObserveLastTime_).count();
            float remaining = llmAutoObserveInterval_ - elapsed;
            if (remaining < 0) remaining = 0;
            ImGui::Text("Next observation in: %.1f s", remaining);
        }
    }

    ImGui::Separator();

    float historyHeight = ImGui::GetContentRegionAvail().y - 120.0f;
    if (historyHeight < 100.0f) historyHeight = 100.0f;

    ImGui::BeginChild("ChatHistory", ImVec2(0, historyHeight), true);
    {
        const auto& history = llmClient_->GetHistory();
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& msg = history[i];
            if (msg.role == "user") {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "You:");
                ImGui::SameLine();
                if (!msg.images.empty()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Image x%d]",
                        static_cast<int>(msg.images.size()));
                    ImGui::SameLine();
                }
            } else {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LAVI:");
                ImGui::SameLine();
            }
            std::string display = (msg.role == "assistant")
                ? KnowledgeBase::StripLearnNotes(ActionPipeline::StripActionTags(StripWebActions(StripEmotionTag(msg.content))))
                : msg.content;
            ImGui::TextWrapped("%s", display.c_str());
            ImGui::Spacing();
        }

        if (isLLMProcessing_) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "LAVI:");
            ImGui::SameLine();
            std::string streamDisplay;
            {
                std::lock_guard<std::mutex> lock(llmStreamMutex_);
                streamDisplay = KnowledgeBase::StripLearnNotes(ActionPipeline::StripActionTags(StripWebActions(StripEmotionTag(llmStreamingText_))));
            }
            ImGui::TextWrapped("%s", streamDisplay.empty() ? "..." : streamDisplay.c_str());
        }

        if (llmAutoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Check async result
    if (isLLMProcessing_ && llmFuture_.valid() &&
        llmFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        lastLLMResponse_ = llmFuture_.get();
        if (!lastLLMResponse_.success && !lastLLMResponse_.error.empty()) {
            llmClient_->AddMessage("assistant", "[Error] " + lastLLMResponse_.error);
        } else if (lastLLMResponse_.success) {
            // 知識の自動収集: 応答内の [learn:...] を KB に保存する（LLM主導）。
            if (knowledgeBase_) {
                for (const auto& note : KnowledgeBase::ParseLearnNotes(lastLLMResponse_.content)) {
                    knowledgeBase_->AddLearnedNote(note);
                }
            }
            auto actions = ParseWebActions(lastLLMResponse_.content);
            if (!actions.empty()) {
                LongTermMemory* ltm = memoryPanel_ ? memoryPanel_->GetLongTermMemory() : nullptr;
                ExecuteWebActions(actions, ltm);
            }
            if (actionPipeline_) actionPipeline_->ParseAndExecute(lastLLMResponse_.content);
            if (memoryPanel_) {
                // 記憶には [learn:...] タグを除いた本文を残す。
                memoryPanel_->GetConversationMemory()->PushMessage(
                    "assistant", KnowledgeBase::StripLearnNotes(lastLLMResponse_.content));
            }
        }
        isLLMProcessing_ = false;
        llmStreamingText_.clear();
    }

    // Local LLM async result
    if (isLLMProcessing_ && localFuture_.valid() &&
        localFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::string out = localFuture_.get();
        if (activeStreamCb_) activeStreamCb_("", true); // 合成パイプラインの終端

        lastLLMResponse_ = LLMResponse{};
        lastLLMResponse_.content = out;
        lastLLMResponse_.success = !out.empty();

        if (!out.empty()) {
            // 知識の自動収集: [learn:...] を KB に保存してから本文から除去する（LLM主導）。
            if (knowledgeBase_) {
                for (const auto& note : KnowledgeBase::ParseLearnNotes(out)) {
                    knowledgeBase_->AddLearnedNote(note);
                }
            }
            out = KnowledgeBase::StripLearnNotes(out);
            lastLLMResponse_.content = out;

            if (suppressActionsForResponse_.load()) {
                // 検索して答えるモード: 動画再生などの暴発を防ぐため [browser:]/[action:] は実行せず除去。
                out = ActionPipeline::StripActionTags(StripWebActions(out));
                lastLLMResponse_.content = out;
            } else {
                auto actions = ParseWebActions(out);
                if (!actions.empty()) {
                    LongTermMemory* ltm = memoryPanel_ ? memoryPanel_->GetLongTermMemory() : nullptr;
                    ExecuteWebActions(actions, ltm);
                }
                if (actionPipeline_) actionPipeline_->ParseAndExecute(out);
            }
            llmClient_->AddMessage("assistant", out);
            if (memoryPanel_) {
                memoryPanel_->GetConversationMemory()->PushMessage("assistant", out);
            }
        }
        isLLMProcessing_ = false;
        llmStreamingText_.clear();
    }

    // Stop synth worker when streaming is done
    if (!isLLMProcessing_) {
        synthPipeline_.StopWorker();
    }

    // Play synthesized audio
    synthPipeline_.UpdatePlayback(ctx_->voiceVox, ctx_->isSpeaking, ctx_->speakFuture);

    // Auto-observe
    if (llmAutoObserve_ && !isLLMProcessing_ && !ctx_->config.apiKey.empty()) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - llmAutoObserveLastTime_).count();
        if (elapsed >= llmAutoObserveInterval_) {
            llmAutoObserveLastTime_ = now;

            std::vector<LLMClient::ImageFrame> frames;
            // 動画モードでは時系列フレームを複数枚送る。holds でサンプルを生存させる。
            std::vector<std::shared_ptr<const CapturedFrame>> holds;
            const bool videoMode = ctx_->config.visionVideoEnabled;
            const int sampleK    = (ctx_->config.visionVideoFrames < 1) ? 1 : ctx_->config.visionVideoFrames;
            auto addSrc = [&](FrameHistory* hist, const std::shared_ptr<const CapturedFrame>& latest) {
                std::vector<std::shared_ptr<const CapturedFrame>> s;
                if (videoMode && hist) s = hist->Sample(sampleK);
                else if (latest) s.push_back(latest);
                for (auto& f : s) {
                    if (!f || f->pixels.empty()) continue;
                    holds.push_back(f);
                    frames.push_back({f->pixels.data(), f->width, f->height});
                }
            };
            if (llmAutoObserveWebCam_) addSrc(ctx_->cameraHistory, ctx_->cameraFrame);
            if (llmAutoObserveScreen_) addSrc(ctx_->screenHistory, ctx_->screenFrame);

            if (!frames.empty()) {
                std::string observePrompt =
                    "[自律観察] 今のカメラ/画面の様子を見てください。"
                    "何か気になること、面白いこと、コメントしたいことがあれば自然に話しかけてください。"
                    "特に何もなければ「特になし」とだけ答えてください。";

                SendLLMRequest(observePrompt, frames);
            }
        }
    }

    if (lastLLMResponse_.inputTokens > 0 || lastLLMResponse_.outputTokens > 0) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tokens: in=%d out=%d",
            lastLLMResponse_.inputTokens, lastLLMResponse_.outputTokens);
        if (lastLLMResponse_.cacheReadInputTokens > 0 || lastLLMResponse_.cacheCreationInputTokens > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), " | Cache: read=%d create=%d",
                lastLLMResponse_.cacheReadInputTokens, lastLLMResponse_.cacheCreationInputTokens);
        }
    }

    // User input area
    bool enterPressed = false;
    if (llmUseWhisper_ && !ctx_->transcribedText.empty()) {
        llmUserInput_ = ctx_->transcribedText;
    }

    if (llmUseWhisper_) ImGui::BeginDisabled();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 160);
    if (ImGui::InputText("##llm_input", llmUserInput_.data(), llmUserInput_.capacity() + 1,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            }, &llmUserInput_)) {
        enterPressed = true;
    }
    ImGui::PopItemWidth();
    if (llmUseWhisper_) ImGui::EndDisabled();

    ImGui::SameLine();

    bool backendReady;
    if (useLocalLLM_) {
        LocalLLM* local = memoryPanel_ ? memoryPanel_->GetLocalLLM() : nullptr;
        backendReady = local && local->IsModelLoaded();
    } else {
        backendReady = !ctx_->config.apiKey.empty();
    }
    bool canSend = !llmUserInput_.empty() && backendReady && !isLLMProcessing_;
    if (!canSend) ImGui::BeginDisabled();

    if (ImGui::Button("Send##llm") || (enterPressed && canSend)) {
        // 発話から興味関心を学習
        if (memoryPanel_) {
            LearnInterestsFromSpeech(llmUserInput_, memoryPanel_->GetLongTermMemory());
        }

        // 行動（アプリ/サービス操作）は LLM が応答内の [action:...] タグで判断・実行する。
        // キーワード事前判定は廃止。実行は応答受信時（ParseAndExecute）に行う。
        {
            std::vector<LLMClient::ImageFrame> frames;
            // 動画モードでは時系列フレームを複数枚送る。holds で SendLLMRequest のコピーまで生存させる。
            std::vector<std::shared_ptr<const CapturedFrame>> holds;
            const bool videoMode = ctx_->config.visionVideoEnabled;
            const int sampleK    = (ctx_->config.visionVideoFrames < 1) ? 1 : ctx_->config.visionVideoFrames;
            auto addSrc = [&](FrameHistory* hist, const std::shared_ptr<const CapturedFrame>& latest) {
                std::vector<std::shared_ptr<const CapturedFrame>> s;
                if (videoMode && hist) s = hist->Sample(sampleK);
                else if (latest) s.push_back(latest);
                for (auto& f : s) {
                    if (!f || f->pixels.empty()) continue;
                    holds.push_back(f);
                    frames.push_back({f->pixels.data(), f->width, f->height});
                }
            };
            if (llmAttachWebCam_) addSrc(ctx_->cameraHistory, ctx_->cameraFrame);
            if (llmAttachScreen_) addSrc(ctx_->screenHistory, ctx_->screenFrame);

            SendLLMRequest(llmUserInput_, frames, llmPlayFiller_);
            llmUserInput_.clear();
        }
    }

    if (!canSend) ImGui::EndDisabled();

    ImGui::SameLine();

    if (isLLMProcessing_) {
        if (ImGui::Button("Cancel##llm")) {
            llmClient_->Cancel();
        }
    } else {
        if (ImGui::Button("Clear##llm")) {
            llmClient_->ClearHistory();
        }
    }
}
