#include "PersonaGenerationSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "system/ui/UIRegistry.h"
#include "system/component/ui/UITextInputComponent.h"

#include "component/ComponentArray.h"

#include <string>

namespace {
constexpr const char* kAction = "llm.persona.generate";

// 説明文 → LAVI 用システムプロンプトを生成させるメタプロンプト。
const char* kPersonaMetaPrompt =
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
} // namespace

PersonaGenerationSystem::PersonaGenerationSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

PersonaGenerationSystem::~PersonaGenerationSystem() = default;

void PersonaGenerationSystem::Initialize() {
    status_ = "ペルソナの説明を入力して Enter（例: 元気な妹）";
    // テキスト入力欄の確定 → ペルソナ生成を開始（main スレッドで発火）。
    UIActionRegistry::Get().RegisterText(kAction, [this](const std::string& text) {
        StartGeneration(text);
    });
}

void PersonaGenerationSystem::Finalize() {
    UIActionRegistry::Get().UnregisterText(kAction);
    if (client_) {
        client_->Cancel();
    }
    if (future_.valid()) {
        future_.wait();
    }
    client_.reset();
}

void PersonaGenerationSystem::StartGeneration(const std::string& description) {
    if (generating_) return; // 多重起動防止
    if (description.empty()) return;

    SharedMediaContext& ctx = LaviContext::Get();
    if (ctx.config.apiKey.empty()) {
        status_ = "API キーが未設定です（LLM 設定の API キーを入力）";
        return;
    }

    client_ = std::make_unique<LLMClient>();
    client_->SetApiKey(ctx.config.apiKey);
    client_->SetModel("claude-haiku-4-5-20251001"); // ペルソナ生成は安価・高速な haiku
    client_->SetMaxTokens(2048);
    client_->AddMessage("user", std::string(kPersonaMetaPrompt) + description);

    future_     = client_->SendAsync();
    generating_ = true;
    status_     = "生成中...";
}

void PersonaGenerationSystem::Update() {
    // 生成完了の取り込み。
    if (generating_ && future_.valid() &&
        future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        LLMResponse res = future_.get();
        if (res.success && !res.content.empty()) {
            SharedMediaContext& ctx = LaviContext::Get();
            ctx.config.llmSystemPrompt = res.content;
            SaveAppConfig(ctx.config);
            status_ = "生成完了：システムプロンプトを更新しました";
        } else {
            status_ = "生成失敗：" + (res.error.empty() ? std::string("不明なエラー") : res.error);
        }
        generating_ = false;
        client_.reset();
    }

    // 入力欄プレースホルダに状態を反映（フォーカス中は触らない）。
    auto* arr = GetComponentArray<UITextInputComponent>();
    if (!arr) {
        return;
    }
    for (auto& slot : arr->GetSlotsRef()) {
        for (auto& ti : slot.components) {
            if (ti.actionId == kAction && !ti.focused) {
                ti.placeholder = status_;
            }
        }
    }
}
