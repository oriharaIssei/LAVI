#pragma once

#include <string>

struct VoiceVoxSynthParams;

enum class EmotionTag { None, Joy, Sadness, Surprise, Anger, Calm, Thinking };

EmotionTag ParseEmotionTag(const std::string& text, size_t& tagEndPos);
VoiceVoxSynthParams EmotionToSynthParams(EmotionTag tag);
std::string StripEmotionTag(const std::string& text);
