#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class VoiceVoxClient;

class SpeechSynthesisPipeline {
public:
    SpeechSynthesisPipeline() = default;
    ~SpeechSynthesisPipeline();

    void StartSession(VoiceVoxClient* voiceVox, int speakerId);
    void FeedDelta(const std::string& delta);
    void FeedDone();
    void StopWorker();
    bool UpdatePlayback(VoiceVoxClient* voiceVox,
                        bool& isSpeaking,
                        std::future<bool>& speakFuture);
    void Shutdown();

private:
    std::string sentenceBuffer_;

    std::deque<std::string> speechQueue_;
    std::mutex speechQueueMutex_;
    std::condition_variable speechQueueCV_;

    std::deque<std::vector<uint8_t>> synthesizedQueue_;
    std::mutex synthesizedMutex_;

    std::thread synthWorker_;
    std::atomic<bool> workerRunning_{false};
};
