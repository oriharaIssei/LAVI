#include "UIPresentationSystem.h"

#include "system/component/ui/UIButtonComponent.h"
#include "system/component/ui/UIElementComponent.h"
#include "system/component/ui/UISliderComponent.h"
#include "system/component/ui/UITabBarComponent.h"
#include "system/component/ui/UITextInputComponent.h"
#include "system/component/ui/UIChatLogComponent.h"
#include "system/component/ui/UIComboComponent.h"
#include "system/component/ui/UICheckboxComponent.h"

#include "system/LaviContext.h"
#include "system/SharedMediaContext.h"
#include "system/ConversationMemory.h"

#include "component/ComponentArray.h"
#include "component/renderer/Sprite.h"
#include "component/text/TextComponent.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using OriGine::EntityHandle;
using OriGine::Vec2f;
using OriGine::Vec4f;

namespace {
// UTF-8 先頭バイトから 1 コードポイントのバイト長を返す（不正なら 1）。
int Utf8Len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}
// 先頭バイトからおおよそのコードポイントを得る（幅推定にのみ使用）。
unsigned int Utf8CodePoint(const std::string& s, size_t i, int len) {
    unsigned int cp = static_cast<unsigned char>(s[i]);
    if (len == 2) cp = ((cp & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
    else if (len == 3) cp = ((cp & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
    else if (len == 4) cp = ((cp & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
    return cp;
}
// テキストを推定幅 budgetPx で折り返して表示行へ分割する（'\n' は強制改行）。
// 幅推定: ASCII≒0.55*fontSize、それ以外（日本語等）≒1.0*fontSize と保守的に見積もる
// （実レイアウトより広めに見積もり、横方向のはみ出しを避ける）。
std::vector<std::string> WrapToLines(const std::string& s, float budgetPx, float fontSize) {
    std::vector<std::string> lines;
    std::string line;
    float w = 0.0f;
    const float halfW = fontSize * 0.55f;
    const float fullW = fontSize;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\n') {
            lines.push_back(line);
            line.clear();
            w = 0.0f;
            ++i;
            continue;
        }
        if (s[i] == '\r') { ++i; continue; }
        const int len = Utf8Len(static_cast<unsigned char>(s[i]));
        if (i + static_cast<size_t>(len) > s.size()) break;
        const unsigned int cp = Utf8CodePoint(s, i, len);
        const float cw = (cp < 0x80) ? halfW : fullW;
        if (!line.empty() && w + cw > budgetPx) {
            lines.push_back(line);
            line.clear();
            w = 0.0f;
        }
        line.append(s, i, static_cast<size_t>(len));
        w += cw;
        i += static_cast<size_t>(len);
    }
    lines.push_back(line); // 末尾行（空でも 1 行として扱う）
    return lines;
}

// 1 行テキストの推定表示幅（px）。WrapToLines と同じ係数（ASCII≒0.55*fontSize、他≒1.0*fontSize）。
float EstimateLineWidth(const std::string& s, float fontSize) {
    float w = 0.0f;
    const float halfW = fontSize * 0.55f;
    for (size_t i = 0; i < s.size();) {
        const int len = Utf8Len(static_cast<unsigned char>(s[i]));
        if (i + static_cast<size_t>(len) > s.size()) break;
        const unsigned int cp = Utf8CodePoint(s, i, len);
        w += (cp < 0x80) ? halfW : fontSize;
        i += static_cast<size_t>(len);
    }
    return w;
}

// 1 行が budgetPx を超える場合、収まる範囲まで切り詰めて末尾に "..." を付ける（改行を含まない前提）。
std::string TruncateLine(const std::string& s, float budgetPx, float fontSize) {
    if (budgetPx <= 0.0f) return s;
    if (EstimateLineWidth(s, fontSize) <= budgetPx) return s;
    const float halfW = fontSize * 0.55f;
    const float ellW  = halfW * 3.0f;                       // "..." の推定幅
    const float limit = (budgetPx > ellW) ? (budgetPx - ellW) : 0.0f;
    std::string out;
    float w = 0.0f;
    for (size_t i = 0; i < s.size();) {
        const int len = Utf8Len(static_cast<unsigned char>(s[i]));
        if (i + static_cast<size_t>(len) > s.size()) break;
        const unsigned int cp = Utf8CodePoint(s, i, len);
        const float cw = (cp < 0x80) ? halfW : fontSize;
        if (w + cw > limit) break;
        out.append(s, i, static_cast<size_t>(len));
        w += cw;
        i += static_cast<size_t>(len);
    }
    out += "...";
    return out;
}

// 複数行テキストの各行を個別に省略する（'\n' 区切り）。
std::string EllipsizeMultiline(const std::string& s, float budgetPx, float fontSize) {
    std::string out;
    size_t start = 0;
    while (true) {
        const size_t nl = s.find('\n', start);
        const std::string line =
            s.substr(start, (nl == std::string::npos) ? std::string::npos : nl - start);
        out += TruncateLine(line, budgetPx, fontSize);
        if (nl == std::string::npos) break;
        out += "\n";
        start = nl + 1;
    }
    return out;
}

// 展開中コンボなど、最前面へ持ち上げる要素に与える描画優先度。通常 UI（〜330）より十分大きく取る。
constexpr int32_t kFrontPriority = 100000;

// 矩形 {x, y, w, h} 同士の重なり判定（軸平行 AABB）。
struct Rect {
    float x, y, w, h;
};
bool RectsOverlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}
} // namespace

UIPresentationSystem::UIPresentationSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Render, -100) {} // SpriteRender/TextRender より先に流す

UIPresentationSystem::~UIPresentationSystem() = default;

void UIPresentationSystem::Initialize() {}

void UIPresentationSystem::Finalize() {
    basePrio_.clear();
}

void UIPresentationSystem::Update() {
    auto* elemArr = GetComponentArray<UIElementComponent>();
    if (!elemArr) {
        return;
    }

    // Sprite と Text は別パスで描画され、Text は常に全 Sprite より前面に出る。そのため展開中コンボの
    // ドロップダウン背景（Sprite）は、その下に重なる他要素の Text を覆い隠せない。これを解決するため、
    // (1) 展開中コンボを最前面優先度へ持ち上げ、(2) ドロップダウン矩形に重なる他要素の Text を隠す。
    struct Occluder {
        EntityHandle owner;
        Rect rect;
    };
    struct ElemRect {
        EntityHandle owner;
        Rect rect;
        bool visible;
    };
    std::vector<Occluder> occluders;
    std::vector<ElemRect> elems;

    for (auto& slot : elemArr->GetSlotsRef()) {
        const EntityHandle owner = slot.owner;
        for (auto& e : slot.components) {
            Vec4f bgColor   = {0.16f, 0.18f, 0.22f, 1.0f};
            Vec4f textColor = {0.92f, 0.94f, 0.98f, 1.0f};
            std::string text;
            bool hasText = false;
            bool ellipsize = false; // true のとき、要素幅を超える行末を "..." に省略（単一行ウィジェット用）
            bool raiseToFront  = false; // 展開中コンボなど、背景＋文字を最前面へ持ち上げるか
            float textMaxWidth = 0.0f; // >0 のとき TextComponent の折り返し幅に反映（チャット履歴用）
            float overrideSpriteH = 0.0f; // >0 のとき背景スプライト高さを上書き（コンボ展開用）

            UISliderComponent* slider = nullptr;

            if (auto* btn = GetComponent<UIButtonComponent>(owner)) {
                bgColor   = e.pressed ? btn->pressColor : (e.hovered ? btn->hoverColor : btn->color);
                textColor = e.hovered ? Vec4f{1.0f, 1.0f, 0.6f, 1.0f} : Vec4f{0.92f, 0.94f, 0.98f, 1.0f};
                text      = btn->label;
                hasText   = true;
                ellipsize = true;
            } else if (auto* cb = GetComponent<UICheckboxComponent>(owner)) {
                bgColor   = e.hovered ? Vec4f{0.20f, 0.24f, 0.32f, 1.0f} : Vec4f{0.14f, 0.16f, 0.20f, 1.0f};
                textColor = cb->checked ? Vec4f{0.70f, 0.90f, 0.75f, 1.0f} : Vec4f{0.80f, 0.82f, 0.86f, 1.0f};
                text      = (cb->checked ? "[x] " : "[ ] ") + cb->label;
                hasText   = true;
                ellipsize = true;
            } else if (auto* sld = GetComponent<UISliderComponent>(owner)) {
                slider  = sld;
                bgColor = {0.18f, 0.20f, 0.26f, 1.0f};
                char buf[160];
                std::snprintf(buf, sizeof(buf), "%s: %.2f", sld->label.c_str(), sld->value);
                text    = buf;
                hasText = true;
                ellipsize = true;
            } else if (auto* tb = GetComponent<UITabBarComponent>(owner)) {
                bgColor = {0.13f, 0.14f, 0.18f, 1.0f};
                if (!tb->tabs.empty() && tb->activeIndex >= 0 && tb->activeIndex < static_cast<int>(tb->tabs.size())) {
                    text    = "< " + tb->tabs[tb->activeIndex].label + " >";
                    hasText = true;
                    ellipsize = true;
                }
            } else if (auto* ti = GetComponent<UITextInputComponent>(owner)) {
                // フォーカス中は枠を明るく。テキストが空ならプレースホルダを淡色で表示。
                bgColor = ti->focused ? Vec4f{0.20f, 0.24f, 0.32f, 1.0f} : Vec4f{0.14f, 0.16f, 0.20f, 1.0f};
                if (ti->text.empty()) {
                    text      = ti->placeholder;
                    textColor = {0.55f, 0.58f, 0.64f, 1.0f}; // プレースホルダは淡色
                } else if (ti->password) {
                    // 伏字（API キー等）: UTF-8 コードポイント数ぶん '*' を並べる。
                    int cps = 0;
                    for (size_t i = 0; i < ti->text.size(); i += static_cast<size_t>(Utf8Len(static_cast<unsigned char>(ti->text[i])))) {
                        ++cps;
                    }
                    text.assign(static_cast<size_t>(cps), '*');
                } else {
                    text = ti->text;
                }
                if (ti->focused) {
                    text += "|"; // 簡易キャレット
                }
                hasText = true;
            } else if (auto* log = GetComponent<UIChatLogComponent>(owner)) {
                // 会話メモリの直近エントリを整形 → 自前で行に折り返し → 表示可能行数ぶんだけ
                // スクロール位置に応じて切り出す（TextComponent にクリップが無いため行ウィンドウ方式）。
                bgColor = {0.10f, 0.11f, 0.14f, 1.0f};
                std::string full;
                if (ConversationMemory* cm = LaviContext::Get().conversationMemory) {
                    const auto& entries = cm->GetRecentEntries();
                    const int n     = static_cast<int>(entries.size());
                    const int take  = (log->maxTurns > 0 && log->maxTurns < n) ? log->maxTurns : n;
                    const int start = n - take;
                    for (int i = start; i < n; ++i) {
                        const std::string& role = entries[i].role;
                        const std::string& prefix =
                            (role == "assistant" || role == "model") ? log->assistantPrefix : log->userPrefix;
                        if (!full.empty()) full += "\n";
                        full += prefix + entries[i].content;
                    }
                }
                if (full.empty()) full = "(まだ会話がありません)";

                // TextComponent から実フォント設定を読み、可視行数を算出する。
                float fontSize = 16.0f, lineSpacing = 1.15f;
                if (auto* t = GetComponent<OriGine::TextComponent>(owner)) {
                    fontSize = t->fontSize; lineSpacing = t->lineSpacing;
                }
                const float budgetPx = (e.size[0] > 28.0f) ? (e.size[0] - 28.0f) : e.size[0];
                std::vector<std::string> lines = WrapToLines(full, budgetPx, fontSize);

                const float lineH = fontSize * (lineSpacing > 0.0f ? lineSpacing : 1.0f);
                int visible = (lineH > 0.0f) ? static_cast<int>((e.size[1] - 12.0f) / lineH) : 1;
                if (visible < 1) visible = 1;
                const int total   = static_cast<int>(lines.size());
                const int maxOff  = (total > visible) ? (total - visible) : 0;
                if (log->scrollOffset > maxOff) log->scrollOffset = maxOff; // 上限クランプ
                if (log->scrollOffset < 0) log->scrollOffset = 0;
                int begin = total - visible - log->scrollOffset; // scrollOffset=0 で最新（末尾）を表示
                if (begin < 0) begin = 0;
                const int end = (begin + visible < total) ? (begin + visible) : total;
                for (int i = begin; i < end; ++i) {
                    if (i > begin) text += "\n";
                    text += lines[i];
                }
                hasText = true;
                // 自前で折り返し済み。安全網として要素幅で折り返す（推定誤差の横はみ出し防止）。
                textMaxWidth = (e.size[0] > 16.0f) ? (e.size[0] - 16.0f) : 0.0f;
            } else if (auto* combo = GetComponent<UIComboComponent>(owner)) {
                // ヘッダ（現在の選択 or プレースホルダ）＋展開中は選択肢一覧を行表示。
                bgColor = e.hovered ? Vec4f{0.20f, 0.24f, 0.32f, 1.0f} : Vec4f{0.14f, 0.16f, 0.20f, 1.0f};
                const int n = static_cast<int>(combo->items.size());
                std::string header;
                if (combo->selectedIndex >= 0 && combo->selectedIndex < n) {
                    header = combo->items[combo->selectedIndex];
                } else {
                    header = combo->placeholder;
                    textColor = {0.55f, 0.58f, 0.64f, 1.0f};
                }
                // ヘッダの機器名は矢印（"  ▼"）の分を残して省略する。展開中の選択肢は
                // 末尾の汎用省略（ellipsize）で各行を収める。
                {
                    float fs = 18.0f;
                    if (auto* t = GetComponent<OriGine::TextComponent>(owner)) fs = t->fontSize;
                    const float budget = (e.size[0] > 16.0f) ? (e.size[0] - 16.0f) : e.size[0];
                    const float arrowReserve = fs * 2.2f; // "  ▼"（半角2＋全角1）の概算幅
                    header = TruncateLine(header, budget - arrowReserve, fs);
                }
                header += combo->expanded ? "  ▲" : "  ▼";
                text = header;
                ellipsize = true;
                if (combo->expanded) {
                    for (int i = 0; i < n; ++i) {
                        text += "\n";
                        text += (i == combo->selectedIndex ? "> " : "  ");
                        text += combo->items[i];
                    }
                    float lineH = 22.0f;
                    if (auto* t = GetComponent<OriGine::TextComponent>(owner)) {
                        lineH = t->fontSize * (t->lineSpacing > 0.0f ? t->lineSpacing : 1.0f);
                    }
                    overrideSpriteH = lineH * static_cast<float>(1 + n); // ヘッダ＋項目数ぶん背景を伸ばす
                    if (e.effectiveVisible) {
                        raiseToFront = true; // 展開中は背景＋文字を最前面へ
                        occluders.push_back({owner, {e.worldPos[0], e.worldPos[1], e.size[0], overrideSpriteH}});
                    }
                }
                hasText = true;
            }

            // 背景（任意）: SpriteRenderer があれば矩形・色・表示を反映
            if (auto* spr = GetComponent<OriGine::SpriteRenderer>(owner)) {
                spr->SetAnchorPoint({0.0f, 0.0f});
                spr->SetTranslate(e.worldPos);
                spr->SetSize(overrideSpriteH > 0.0f ? Vec2f{e.size[0], overrideSpriteH} : e.size);
                spr->SetColor(bgColor);
                spr->SetScale(e.effectiveVisible ? Vec2f{1.0f, 1.0f} : Vec2f{0.0f, 0.0f});
            }

            // スライダーのつまみ（任意）: 2 つ目の SpriteRenderer があれば現在値 t の位置へ反映。
            // index0 = トラック背景, index1 = ドラッグつまみ という規約。
            if (slider) {
                if (auto* handle = GetComponent<OriGine::SpriteRenderer>(owner, 1)) {
                    const float t        = slider->Normalized();
                    const float handleW  = 14.0f;
                    const float padY     = 3.0f;
                    const float trackW   = (e.size[0] > handleW) ? (e.size[0] - handleW) : 0.0f;
                    const Vec2f handlePos = {e.worldPos[0] + t * trackW, e.worldPos[1] - padY};
                    const Vec2f handleSize = {handleW, e.size[1] + padY * 2.0f};
                    const Vec4f handleColor =
                        (slider->dragging || e.hovered) ? Vec4f{0.70f, 0.86f, 1.0f, 1.0f}
                                                        : Vec4f{0.55f, 0.72f, 0.96f, 1.0f};
                    handle->SetAnchorPoint({0.0f, 0.0f});
                    handle->SetTranslate(handlePos);
                    handle->SetSize(handleSize);
                    handle->SetColor(handleColor);
                    handle->SetScale(e.effectiveVisible ? Vec2f{1.0f, 1.0f} : Vec2f{0.0f, 0.0f});
                }
            }

            // ラベル（任意）: TextComponent があればテキスト・位置・色・可視を反映
            if (auto* txt = GetComponent<OriGine::TextComponent>(owner)) {
                if (hasText) {
                    if (ellipsize) {
                        // 要素幅（左右 8px パディング）に収まらない行末を "..." に置換する。
                        const float budget = (e.size[0] > 16.0f) ? (e.size[0] - 16.0f) : e.size[0];
                        text = EllipsizeMultiline(text, budget, txt->fontSize);
                    }
                    txt->text = text;
                }
                txt->position = {e.worldPos[0] + 8.0f, e.worldPos[1] + 6.0f};
                txt->color    = textColor;
                txt->visible  = e.effectiveVisible;
                if (textMaxWidth > 0.0f) {
                    txt->maxWidth = textMaxWidth; // チャット履歴は要素幅で折り返す
                }
                txt->dirty    = true;
            }

            // 展開中コンボは背景＋文字を最前面優先度へ持ち上げ、通常時は本来の優先度へ戻す。
            // 本来値は初回（持ち上げ前）に取得して保持する。
            auto* spr = GetComponent<OriGine::SpriteRenderer>(owner);
            auto* txt = GetComponent<OriGine::TextComponent>(owner);
            if (spr || txt) {
                auto it = basePrio_.find(owner);
                if (it == basePrio_.end()) {
                    it = basePrio_.emplace(owner,
                                           std::pair<int32_t, int32_t>{
                                               spr ? spr->GetRenderPriority() : 0,
                                               txt ? txt->renderPriority : 0})
                             .first;
                }
                if (raiseToFront) {
                    if (spr) spr->SetRenderPriority(kFrontPriority);
                    if (txt) txt->renderPriority = kFrontPriority;
                } else {
                    if (spr) spr->SetRenderPriority(it->second.first);
                    if (txt) txt->renderPriority = it->second.second;
                }
            }

            elems.push_back({owner, {e.worldPos[0], e.worldPos[1], e.size[0], e.size[1]}, e.effectiveVisible});
        }
    }

    // 展開中コンボのドロップダウン矩形に重なる「他要素」の Text を隠す（Text のにじみ出し防止）。
    // Sprite 層は最前面へ持ち上げたコンボ背景が覆うため、ここでは Text のみを対象にする。
    for (const auto& el : elems) {
        if (!el.visible) {
            continue;
        }
        for (const auto& oc : occluders) {
            if (oc.owner == el.owner) {
                continue; // コンボ自身の Text は隠さない
            }
            if (RectsOverlap(el.rect, oc.rect)) {
                if (auto* txt = GetComponent<OriGine::TextComponent>(el.owner)) {
                    txt->visible = false;
                }
                break;
            }
        }
    }
}
