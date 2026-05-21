#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "WhisperTranscriber.h"

#include "mediaCapture/Microphone.h"
struct SharedMediaContext;


class MicrophonePanel{
public:
	void Initialize(SharedMediaContext* ctx);
	void Finalize();
	void Draw();

private:
	SharedMediaContext* ctx_ = nullptr;

	std::unique_ptr<OriGine::Microphone> microphone_;
	std::vector<OriGine::MicrophoneDeviceInfo> micDevices_;
	int selectedMicDevice_ = 0;

	std::mutex audioMutex_;
	float currentAudioLevel_ = 0.0f;
	float peakAudioLevel_ = 0.0f;
	std::string recordFilePath_ = "recorded.wav";

	std::unique_ptr<WhisperTranscriber> transcriber_;
	std::string whisperModelPath_ = "application/resource/whisper/ggml-large-v3.bin";
	std::string vadModelPath_ = "application/resource/whisper/ggml-silero-v6.2.0.bin";
	WhisperResult detailedResult_;
	bool showDetailedResult_ = false;
	std::future<bool> transcribeFuture_;
	bool isTranscribing_ = false;
};
