#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "EmotionTag.h"

class VoiceVoxClient;

class SpeechSynthesisPipeline {
public:
    SpeechSynthesisPipeline() = default;
    ~SpeechSynthesisPipeline();

    void StartSession(VoiceVoxClient* voiceVox, int speakerId);
    void QueueFiller();
    void FeedDelta(const std::string& delta);
    void FeedDone();
    void StopWorker();
    bool UpdatePlayback(VoiceVoxClient* voiceVox,
                        bool& isSpeaking,
                        std::future<bool>& speakFuture);
    void Shutdown();

private:
    std::string sentenceBuffer_;
    EmotionTag currentEmotion_ = EmotionTag::None;

    std::deque<std::pair<std::string, EmotionTag>> speechQueue_;
    std::mutex speechQueueMutex_;
    std::condition_variable speechQueueCV_;

    std::deque<std::vector<uint8_t>> synthesizedQueue_;
    std::mutex synthesizedMutex_;

    std::thread synthWorker_;
    std::atomic<bool> workerRunning_{false};
};
