#include "WhisperTranscriber.h"

#include "whisper.h"

#include <cmath>
#include <cstring>

static constexpr uint32_t kWhisperSampleRate = WHISPER_SAMPLE_RATE; // 16000

WhisperTranscriber::WhisperTranscriber() {}

WhisperTranscriber::~WhisperTranscriber() {
    UnloadModel();
}

bool WhisperTranscriber::LoadModel(const std::string& modelPath, int nThreads) {
    UnloadModel();

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true;
    cparams.dtw_token_timestamps = true;

    ctx_ = whisper_init_from_file_with_params(modelPath.c_str(), cparams);
    if (!ctx_) {
        return false;
    }
    nThreads_ = (nThreads <= 0)
        ? static_cast<int>(std::thread::hardware_concurrency())
        : nThreads;
    return true;
}

void WhisperTranscriber::UnloadModel() {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

bool WhisperTranscriber::IsModelLoaded() const {
    return ctx_ != nullptr;
}

void WhisperTranscriber::SetLanguage(const std::string& lang) {
    language_ = lang;
}

void WhisperTranscriber::SetInitialPrompt(const std::string& prompt) {
    initialPrompt_ = prompt;
}

void WhisperTranscriber::SetBeamSize(int beamSize) {
    beamSize_ = beamSize;
}

void WhisperTranscriber::SetVadModelPath(const std::string& path) {
    vadModelPath_ = path;
}

void WhisperTranscriber::PushAudio(const float* samples, uint32_t frameCount, uint32_t channels, uint32_t sampleRate) {
    std::vector<float> mono;
    if (channels > 1) {
        MixToMono(samples, frameCount, channels, mono);
    } else {
        mono.assign(samples, samples + frameCount);
    }

    std::vector<float> resampled;
    if (sampleRate != kWhisperSampleRate) {
        Resample(mono.data(), static_cast<uint32_t>(mono.size()), sampleRate, resampled);
    } else {
        resampled = std::move(mono);
    }

    std::lock_guard<std::mutex> lock(audioMutex_);
    audioBuffer_.insert(audioBuffer_.end(), resampled.begin(), resampled.end());
}

bool WhisperTranscriber::Transcribe() {
    if (!ctx_) return false;

    std::vector<float> pcm;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        if (audioBuffer_.empty()) return false;
        pcm.swap(audioBuffer_);
    }

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    wparams.n_threads            = nThreads_;
    wparams.language             = language_.c_str();
    wparams.translate            = false;
    wparams.no_timestamps        = false;
    wparams.single_segment       = false;
    wparams.print_progress       = false;
    wparams.print_realtime       = false;
    wparams.print_special        = false;
    wparams.print_timestamps     = false;
    wparams.token_timestamps     = true;
    wparams.beam_search.beam_size = beamSize_;

    wparams.suppress_blank   = true;
    wparams.suppress_nst     = true;
    wparams.no_speech_thold  = 0.8f;
    wparams.entropy_thold    = 2.0f;
    wparams.logprob_thold    = -0.5f;
    wparams.temperature      = 0.0f;
    wparams.temperature_inc  = 0.0f;

    if (!vadModelPath_.empty()) {
        wparams.vad            = true;
        wparams.vad_model_path = vadModelPath_.c_str();
        wparams.vad_params.threshold = 0.8f;
    }

    if (!initialPrompt_.empty()) {
        wparams.initial_prompt = initialPrompt_.c_str();
    }

    int ret = whisper_full(ctx_, wparams, pcm.data(), static_cast<int>(pcm.size()));
    if (ret != 0) {
        return false;
    }

    WhisperResult newResult;
    int nSegments = whisper_full_n_segments(ctx_);
    for (int i = 0; i < nSegments; ++i) {
        const char* segText = whisper_full_get_segment_text(ctx_, i);
        std::string text = segText ? segText : "";

        int nTokens = whisper_full_n_tokens(ctx_, i);
        float avgProb = 0.0f;
        for (int j = 0; j < nTokens; ++j) {
            avgProb += whisper_full_get_token_data(ctx_, i, j).p;
        }
        if (nTokens > 0) avgProb /= static_cast<float>(nTokens);

        if (avgProb < 0.4f) continue;

        if (text.find("(") != std::string::npos || text.find("[") != std::string::npos ||
            text.find("（") != std::string::npos || text.find("♪") != std::string::npos) continue;

        WhisperSegment seg;
        seg.text = text;
        seg.t0 = whisper_full_get_segment_t0(ctx_, i);
        seg.t1 = whisper_full_get_segment_t1(ctx_, i);

        for (int j = 0; j < nTokens; ++j) {
            whisper_token_data tdata = whisper_full_get_token_data(ctx_, i, j);
            const char* tokenText = whisper_full_get_token_text(ctx_, i, j);

            WhisperToken tok;
            tok.text        = tokenText ? tokenText : "";
            tok.probability = tdata.p;
            tok.voiceLength = tdata.vlen;
            tok.t0          = tdata.t0;
            tok.t1          = tdata.t1;
            seg.tokens.push_back(std::move(tok));
        }

        newResult.fullText += seg.text;
        newResult.segments.push_back(std::move(seg));
    }

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result_ = std::move(newResult);
    }
    return true;
}

std::string WhisperTranscriber::GetResult() const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    return result_.fullText;
}

WhisperResult WhisperTranscriber::GetDetailedResult() const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    return result_;
}

void WhisperTranscriber::ClearAudio() {
    std::lock_guard<std::mutex> lock(audioMutex_);
    audioBuffer_.clear();
}

size_t WhisperTranscriber::GetAudioSampleCount() const {
    std::lock_guard<std::mutex> lock(audioMutex_);
    return audioBuffer_.size();
}

void WhisperTranscriber::MixToMono(const float* src, uint32_t frameCount, uint32_t channels, std::vector<float>& dst) {
    dst.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        float sum = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            sum += src[i * channels + ch];
        }
        dst[i] = sum / static_cast<float>(channels);
    }
}

void WhisperTranscriber::Resample(const float* src, uint32_t srcFrames, uint32_t srcRate, std::vector<float>& dst) {
    double ratio = static_cast<double>(kWhisperSampleRate) / static_cast<double>(srcRate);
    uint32_t dstFrames = static_cast<uint32_t>(std::ceil(srcFrames * ratio));
    dst.resize(dstFrames);

    for (uint32_t i = 0; i < dstFrames; ++i) {
        double srcPos = i / ratio;
        uint32_t idx = static_cast<uint32_t>(srcPos);
        float frac = static_cast<float>(srcPos - idx);

        if (idx + 1 < srcFrames) {
            dst[i] = src[idx] * (1.0f - frac) + src[idx + 1] * frac;
        } else if (idx < srcFrames) {
            dst[i] = src[idx];
        } else {
            dst[i] = 0.0f;
        }
    }
}
