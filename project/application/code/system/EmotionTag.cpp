#include "EmotionTag.h"
#include "VoiceVoxClient.h"

EmotionTag ParseEmotionTag(const std::string& text, size_t& tagEndPos) {
    tagEndPos = 0;
    if (text.empty() || text[0] != '[') return EmotionTag::None;

    auto end = text.find(']');
    if (end == std::string::npos || end > 20) return EmotionTag::None;

    std::string tag = text.substr(1, end - 1);
    tagEndPos = end + 1;

    if (tag == "joy") return EmotionTag::Joy;
    if (tag == "sadness") return EmotionTag::Sadness;
    if (tag == "surprise") return EmotionTag::Surprise;
    if (tag == "anger") return EmotionTag::Anger;
    if (tag == "calm") return EmotionTag::Calm;
    if (tag == "thinking") return EmotionTag::Thinking;

    tagEndPos = 0;
    return EmotionTag::None;
}

VoiceVoxSynthParams EmotionToSynthParams(EmotionTag tag) {
    //                        speed  pitch  intonation  volume  pause
    switch (tag) {
    case EmotionTag::Joy:       return {1.10f,  0.05f, 1.3f, 1.00f, 1.0f};
    case EmotionTag::Sadness:   return {0.85f, -0.05f, 0.7f, 0.85f, 1.5f};
    case EmotionTag::Surprise:  return {1.15f,  0.10f, 1.5f, 1.10f, 0.9f};
    case EmotionTag::Anger:     return {1.05f,  0.00f, 1.4f, 1.15f, 0.8f};
    case EmotionTag::Calm:      return {0.95f,  0.00f, 1.0f, 1.00f, 1.4f};
    case EmotionTag::Thinking:  return {0.85f, -0.02f, 0.8f, 0.90f, 1.6f};
    default:                    return {1.00f,  0.00f, 1.0f, 1.00f, 1.3f};
    }
}

std::string StripEmotionTag(const std::string& text) {
    size_t pos = 0;
    ParseEmotionTag(text, pos);
    return pos > 0 ? text.substr(pos) : text;
}

namespace {
// 全角ダブルクオート: “ = E2 80 9C / ” = E2 80 9D
bool IsCurlyOpen(const std::string& s, size_t i) {
    return i + 2 < s.size() && (unsigned char)s[i] == 0xE2 &&
           (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x9C;
}
bool IsCurlyClose(const std::string& s, size_t i) {
    return i + 2 < s.size() && (unsigned char)s[i] == 0xE2 &&
           (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x9D;
}
}  // namespace

std::string ExtractSpokenText(const std::string& text) {
    std::string out;
    bool inAscii = false;
    bool inCurly = false;
    bool found = false;
    for (size_t i = 0; i < text.size();) {
        if (IsCurlyOpen(text, i)) { inCurly = true; found = true; i += 3; continue; }
        if (IsCurlyClose(text, i)) { inCurly = false; i += 3; continue; }
        if (text[i] == '"') {
            inAscii = !inAscii;
            if (inAscii) found = true;
            ++i;
            continue;
        }
        if (inAscii || inCurly) out += text[i];
        ++i;
    }
    return found ? out : std::string();
}

std::string StripSpeechQuotes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (IsCurlyOpen(text, i) || IsCurlyClose(text, i)) { i += 3; continue; }
        if (text[i] == '"') { ++i; continue; }
        out += text[i++];
    }
    return out;
}
