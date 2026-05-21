#include "SpeechSynthesisPipeline.h"
#include "VoiceVoxClient.h"

#include <cstring>

SpeechSynthesisPipeline::~SpeechSynthesisPipeline() {
    Shutdown();
}

void SpeechSynthesisPipeline::StartSession(VoiceVoxClient* voiceVox, int speakerId) {
    Shutdown();

    sentenceBuffer_.clear();
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
        while (workerRunning_.load()) {
            std::string sentence;
            {
                std::unique_lock<std::mutex> lock(speechQueueMutex_);
                speechQueueCV_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !speechQueue_.empty() || !workerRunning_.load();
                });
                if (!workerRunning_.load() && speechQueue_.empty()) break;
                if (speechQueue_.empty()) continue;
                sentence = std::move(speechQueue_.front());
                speechQueue_.pop_front();
            }
            auto wav = voiceVox->SynthesizeWav(sentence, speakerId);
            if (!wav.empty()) {
                std::lock_guard<std::mutex> lock(synthesizedMutex_);
                synthesizedQueue_.push_back(std::move(wav));
            }
        }
        while (true) {
            std::string sentence;
            {
                std::lock_guard<std::mutex> lock(speechQueueMutex_);
                if (speechQueue_.empty()) break;
                sentence = std::move(speechQueue_.front());
                speechQueue_.pop_front();
            }
            auto wav = voiceVox->SynthesizeWav(sentence, speakerId);
            if (!wav.empty()) {
                std::lock_guard<std::mutex> lock(synthesizedMutex_);
                synthesizedQueue_.push_back(std::move(wav));
            }
        }
    });
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
        if (!sentence.empty()) {
            {
                std::lock_guard<std::mutex> lock(speechQueueMutex_);
                speechQueue_.push_back(std::move(sentence));
            }
            speechQueueCV_.notify_one();
        }
    }
}

void SpeechSynthesisPipeline::FeedDone() {
    if (!sentenceBuffer_.empty()) {
        {
            std::lock_guard<std::mutex> lock(speechQueueMutex_);
            speechQueue_.push_back(std::move(sentenceBuffer_));
        }
        speechQueueCV_.notify_one();
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
