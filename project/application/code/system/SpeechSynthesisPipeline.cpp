#include "SpeechSynthesisPipeline.h"
#include "VoiceVoxClient.h"
#include "EmotionTag.h"

#include <cstring>

SpeechSynthesisPipeline::~SpeechSynthesisPipeline() {
    Shutdown();
}

void SpeechSynthesisPipeline::StartSession(VoiceVoxClient* voiceVox, int speakerId) {
    Shutdown();

    sentenceBuffer_.clear();
    currentEmotion_ = EmotionTag::None;
    {
        std::lock_guard<std::mutex> lock(speechQueueMutex_);
        speechQueue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(synthesizedMutex_);
        synthesizedQueue_.clear();
    }

    workerRunning_.store(true);
    synthWorker_ = std::thread([this, voiceVox, speakerId]() {
        auto synthesizeOne = [&](const std::string& sentence, EmotionTag emotion) {
            VoiceVoxSynthParams params = EmotionToSynthParams(emotion);
            auto wav = voiceVox->SynthesizeWav(sentence, speakerId, params);
            if (!wav.empty()) {
                std::lock_guard<std::mutex> lock(synthesizedMutex_);
                synthesizedQueue_.push_back(std::move(wav));
            }
        };

        while (workerRunning_.load()) {
            std::string sentence;
            EmotionTag emotion = EmotionTag::None;
            {
                std::unique_lock<std::mutex> lock(speechQueueMutex_);
                speechQueueCV_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !speechQueue_.empty() || !workerRunning_.load();
                });
                if (!workerRunning_.load() && speechQueue_.empty()) break;
                if (speechQueue_.empty()) continue;
                sentence = std::move(speechQueue_.front().first);
                emotion = speechQueue_.front().second;
                speechQueue_.pop_front();
            }
            synthesizeOne(sentence, emotion);
        }
        while (true) {
            std::string sentence;
            EmotionTag emotion = EmotionTag::None;
            {
                std::lock_guard<std::mutex> lock(speechQueueMutex_);
                if (speechQueue_.empty()) break;
                sentence = std::move(speechQueue_.front().first);
                emotion = speechQueue_.front().second;
                speechQueue_.pop_front();
            }
            synthesizeOne(sentence, emotion);
        }
    });
}

void SpeechSynthesisPipeline::QueueFiller() {
    static const char* kFillers[] = {
        "\xe3\x81\x88\xe3\x81\xa3\xe3\x81\xa8",       // えっと
        "\xe3\x82\x93\xe3\x83\xbc",                     // んー
        "\xe3\x81\x82\xe3\x83\xbc",                     // あー
    };
    static int sIndex = 0;

    const char* filler = kFillers[sIndex % 3];
    sIndex++;

    {
        std::lock_guard<std::mutex> lock(speechQueueMutex_);
        speechQueue_.push_back({filler, EmotionTag::Thinking});
    }
    speechQueueCV_.notify_one();
}

void SpeechSynthesisPipeline::FeedDelta(const std::string& delta) {
    sentenceBuffer_ += delta;

    size_t splitPos = std::string::npos;
    {
        size_t p = sentenceBuffer_.rfind('\n');
        if (p != std::string::npos && p < sentenceBuffer_.size() - 1)
            splitPos = p;
    }
    const char* delimiters[] = {"。", "！", "？", "!", "?"};
    for (auto* d : delimiters) {
        size_t p = sentenceBuffer_.rfind(d);
        if (p != std::string::npos) {
            size_t dlen = strlen(d);
            if (p + dlen < sentenceBuffer_.size() || splitPos == std::string::npos)
                if (splitPos == std::string::npos || p + dlen > splitPos)
                    splitPos = p + dlen;
        }
    }
    if (splitPos != std::string::npos && splitPos > 0 && splitPos <= sentenceBuffer_.size()) {
        std::string sentence = sentenceBuffer_.substr(0, splitPos);
        sentenceBuffer_ = sentenceBuffer_.substr(splitPos);
        while (!sentenceBuffer_.empty() && (sentenceBuffer_[0] == '\n' || sentenceBuffer_[0] == ' '))
            sentenceBuffer_.erase(0, 1);

        size_t tagEnd = 0;
        EmotionTag tag = ParseEmotionTag(sentence, tagEnd);
        if (tag != EmotionTag::None) {
            currentEmotion_ = tag;
            sentence = sentence.substr(tagEnd);
        }

        if (!sentence.empty()) {
            {
                std::lock_guard<std::mutex> lock(speechQueueMutex_);
                speechQueue_.push_back({std::move(sentence), currentEmotion_});
            }
            speechQueueCV_.notify_one();
        }
    }
}

void SpeechSynthesisPipeline::FeedDone() {
    if (!sentenceBuffer_.empty()) {
        size_t tagEnd = 0;
        EmotionTag tag = ParseEmotionTag(sentenceBuffer_, tagEnd);
        if (tag != EmotionTag::None) {
            currentEmotion_ = tag;
            sentenceBuffer_ = sentenceBuffer_.substr(tagEnd);
        }
        if (!sentenceBuffer_.empty()) {
            {
                std::lock_guard<std::mutex> lock(speechQueueMutex_);
                speechQueue_.push_back({std::move(sentenceBuffer_), currentEmotion_});
            }
            speechQueueCV_.notify_one();
        }
        sentenceBuffer_.clear();
    }
}

void SpeechSynthesisPipeline::StopWorker() {
    if (workerRunning_.load()) {
        workerRunning_.store(false);
        speechQueueCV_.notify_one();
    }
}

bool SpeechSynthesisPipeline::UpdatePlayback(
    VoiceVoxClient* voiceVox,
    bool& isSpeaking,
    std::future<bool>& speakFuture) {
    if (!isSpeaking && voiceVox) {
        std::lock_guard<std::mutex> lock(synthesizedMutex_);
        if (!synthesizedQueue_.empty()) {
            auto wav = std::move(synthesizedQueue_.front());
            synthesizedQueue_.pop_front();
            isSpeaking = true;
            speakFuture = voiceVox->PlayWavDataAsync(std::move(wav));
            return true;
        }
    }
    return false;
}

void SpeechSynthesisPipeline::Shutdown() {
    workerRunning_.store(false);
    speechQueueCV_.notify_one();
    if (synthWorker_.joinable()) synthWorker_.join();
}
