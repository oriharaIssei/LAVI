#include "MicGatekeeper.h"

#include <algorithm>

namespace {
constexpr size_t kOverlap = 16;  // 差分境界をまたぐキーワードを拾うための重なり
}  // namespace

std::string MicGatekeeper::ToLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        // ASCII の A-Z のみ小文字化。マルチバイト (UTF-8 日本語) はそのまま残す。
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'A' && uc <= 'Z') c = static_cast<char>(uc - 'A' + 'a');
    }
    return out;
}

void MicGatekeeper::SetKeywords(const std::vector<std::string>& kws) {
    keywords_.clear();
    for (const auto& k : kws) {
        if (!k.empty()) keywords_.push_back(k);
    }
}

MicGateResult MicGatekeeper::Evaluate(const std::string& transcribedText) {
    MicGateResult result;
    if (transcribedText == lastText_ || keywords_.empty()) {
        lastText_ = transcribedText;
        return result;
    }

    // lastText_ と transcribedText の共通接頭辞長を求める
    size_t cpl = 0;
    const size_t n = (std::min)(lastText_.size(), transcribedText.size());
    while (cpl < n && lastText_[cpl] == transcribedText[cpl]) ++cpl;

    // 走査開始位置 (境界をまたぐキーワードのため少し手前から)
    const size_t scanStart = (cpl > kOverlap) ? (cpl - kOverlap) : 0;
    const std::string newPortion = transcribedText.substr(scanStart);

    const std::string haystack = caseSensitive_ ? newPortion : ToLowerAscii(newPortion);
    for (const auto& kw : keywords_) {
        const std::string needle = caseSensitive_ ? kw : ToLowerAscii(kw);
        if (!needle.empty() && haystack.find(needle) != std::string::npos) {
            result.matched.push_back(kw);
        }
    }

    result.newText = (cpl < transcribedText.size()) ? transcribedText.substr(cpl) : std::string();
    result.triggered = !result.matched.empty();
    lastText_ = transcribedText;
    return result;
}

void MicGatekeeper::ResetState() {
    lastText_.clear();
}
