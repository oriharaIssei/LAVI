#pragma once

#include <string>

struct VoiceVoxSynthParams;

enum class EmotionTag { None, Joy, Sadness, Surprise, Anger, Calm, Thinking };

EmotionTag ParseEmotionTag(const std::string& text, size_t& tagEndPos);
VoiceVoxSynthParams EmotionToSynthParams(EmotionTag tag);
std::string StripEmotionTag(const std::string& text);

// 読み上げ対象の抽出: "..." (半角) / “...” (全角) で囲まれた部分のみを連結して返す。
// クオートが1つも無ければ空文字列を返す (呼び出し側でフォールバック判断する)。
std::string ExtractSpokenText(const std::string& text);

// ストリーミング経路向け: 文中のダブルクオート (" “ ”) のみを除去する。
std::string StripSpeechQuotes(const std::string& text);
