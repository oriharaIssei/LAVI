#include "VoiceVoxClient.h"

#include <curl/curl.h>

#include <cstring>
#include <sstream>
#include <thread>

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

size_t WriteBinaryCallback(void* contents, size_t size, size_t nmemb, std::vector<uint8_t>* output) {
    size_t totalSize = size * nmemb;
    auto* bytes = static_cast<uint8_t*>(contents);
    output->insert(output->end(), bytes, bytes + totalSize);
    return totalSize;
}

std::string UrlEncode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) return str;
    char* encoded = curl_easy_escape(curl, str.c_str(), static_cast<int>(str.size()));
    std::string result = encoded ? encoded : str;
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

// WAV header parsing
struct WavHeader {
    uint16_t channels;
    uint32_t sampleRate;
    uint16_t bitsPerSample;
    uint32_t dataOffset;
    uint32_t dataSize;
};

bool ParseWavHeader(const std::vector<uint8_t>& data, WavHeader& header) {
    if (data.size() < 44) return false;
    if (memcmp(data.data(), "RIFF", 4) != 0) return false;
    if (memcmp(data.data() + 8, "WAVE", 4) != 0) return false;

    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        char chunkId[5] = {};
        memcpy(chunkId, data.data() + pos, 4);
        uint32_t chunkSize = 0;
        memcpy(&chunkSize, data.data() + pos + 4, 4);

        if (strcmp(chunkId, "fmt ") == 0 && chunkSize >= 16) {
            memcpy(&header.channels, data.data() + pos + 10, 2);
            memcpy(&header.sampleRate, data.data() + pos + 12, 4);
            memcpy(&header.bitsPerSample, data.data() + pos + 22, 2);
        } else if (strcmp(chunkId, "data") == 0) {
            header.dataOffset = static_cast<uint32_t>(pos + 8);
            header.dataSize = chunkSize;
            return true;
        }
        pos += 8 + chunkSize;
        if (chunkSize % 2 != 0) ++pos;
    }
    return false;
}

// Simple JSON string extractor
std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos += searchKey.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

int ExtractJsonInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    auto pos = json.find(searchKey);
    if (pos == std::string::npos) return -1;
    pos += searchKey.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    std::string num;
    while (pos < json.size() && (json[pos] >= '0' && json[pos] <= '9')) {
        num += json[pos++];
    }
    return num.empty() ? -1 : std::stoi(num);
}

} // namespace

VoiceVoxClient::VoiceVoxClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    InitXAudio2();
}

VoiceVoxClient::~VoiceVoxClient() {
    Stop();
    ShutdownXAudio2();
    StopEngine();
    curl_global_cleanup();
}

bool VoiceVoxClient::InitXAudio2() {
    HRESULT hr = XAudio2Create(&xaudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) return false;

    hr = xaudio2_->CreateMasteringVoice(&masterVoice_);
    if (FAILED(hr)) {
        xaudio2_->Release();
        xaudio2_ = nullptr;
        return false;
    }
    return true;
}

void VoiceVoxClient::ShutdownXAudio2() {
    if (sourceVoice_) {
        sourceVoice_->Stop();
        sourceVoice_->DestroyVoice();
        sourceVoice_ = nullptr;
    }
    if (masterVoice_) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }
    if (xaudio2_) {
        xaudio2_->Release();
        xaudio2_ = nullptr;
    }
}

bool VoiceVoxClient::StartEngine(const std::string& enginePath) {
    lastError_.clear();
    externalEngine_ = false;

    if (IsEngineRunning()) return true;

    // 既にポート上で起動済みのインスタンスがあれば再利用
    if (IsEngineReady()) {
        externalEngine_ = true;
        return true;
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::string cmdLine = "\"" + enginePath + "\"";

    if (!CreateProcessA(
        nullptr,
        cmdLine.data(),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi)) {
        DWORD err = GetLastError();
        char buf[128];
        snprintf(buf, sizeof(buf), "CreateProcess failed (error %lu)", err);
        lastError_ = buf;
        return false;
    }

    CloseHandle(pi.hThread);
    engineProcess_ = pi.hProcess;

    // Wait for engine to become ready (up to 30 seconds)
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // プロセスが終了していたら即座にエラー
        if (!IsEngineRunning()) {
            DWORD exitCode = GetEngineExitCode();
            char buf[128];
            snprintf(buf, sizeof(buf), "Engine process exited (code %lu)", exitCode);
            lastError_ = buf;
            CloseHandle(engineProcess_);
            engineProcess_ = nullptr;
            return false;
        }

        if (IsEngineReady()) return true;
    }

    lastError_ = "Engine startup timeout (30s)";
    return false;
}

void VoiceVoxClient::StopEngine() {
    if (engineProcess_) {
        if (!externalEngine_) {
            TerminateProcess(engineProcess_, 0);
        }
        CloseHandle(engineProcess_);
        engineProcess_ = nullptr;
    }
    externalEngine_ = false;
}

bool VoiceVoxClient::IsEngineRunning() const {
    if (externalEngine_) return IsEngineReady();
    if (!engineProcess_) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(engineProcess_, &exitCode)) return false;
    return exitCode == STILL_ACTIVE;
}

DWORD VoiceVoxClient::GetEngineExitCode() const {
    if (!engineProcess_) return 0;
    DWORD exitCode = 0;
    GetExitCodeProcess(engineProcess_, &exitCode);
    return exitCode;
}

bool VoiceVoxClient::IsEngineReady() const {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string response;
    std::string url = baseUrl_ + "/version";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return res == CURLE_OK && !response.empty();
}

std::vector<VoiceVoxSpeaker> VoiceVoxClient::GetSpeakers() {
    std::vector<VoiceVoxSpeaker> speakers;
    std::string json = HttpGet(baseUrl_ + "/speakers");
    if (json.empty()) return speakers;

    // Parse speakers JSON array
    size_t pos = 0;
    while (pos < json.size()) {
        auto namePos = json.find("\"name\":\"", pos);
        if (namePos == std::string::npos) break;

        std::string speakerName = ExtractJsonString(json.substr(namePos), "name");

        auto stylesPos = json.find("\"styles\":", namePos);
        if (stylesPos == std::string::npos) break;

        auto stylesEnd = json.find(']', stylesPos);
        if (stylesEnd == std::string::npos) break;
        std::string stylesBlock = json.substr(stylesPos, stylesEnd - stylesPos + 1);

        size_t stylePos = 0;
        while (stylePos < stylesBlock.size()) {
            auto styleNamePos = stylesBlock.find("\"name\":\"", stylePos);
            if (styleNamePos == std::string::npos) break;

            std::string styleName = ExtractJsonString(stylesBlock.substr(styleNamePos), "name");
            int styleId = ExtractJsonInt(stylesBlock.substr(styleNamePos), "id");

            if (styleId >= 0) {
                speakers.push_back({styleId, speakerName, styleName});
            }
            stylePos = styleNamePos + 1;
        }
        pos = stylesEnd + 1;
    }
    return speakers;
}

std::future<bool> VoiceVoxClient::SpeakAsync(const std::string& text, int speakerId) {
    return std::async(std::launch::async, [this, text, speakerId]() {
        return Speak(text, speakerId);
    });
}

bool VoiceVoxClient::Speak(const std::string& text, int speakerId) {
    auto wavData = Synthesize(text, speakerId);
    if (wavData.empty()) return false;
    return PlayWav(wavData);
}

std::vector<uint8_t> VoiceVoxClient::SynthesizeWav(const std::string& text, int speakerId) {
    return Synthesize(text, speakerId);
}

std::vector<uint8_t> VoiceVoxClient::SynthesizeWav(const std::string& text, int speakerId, const VoiceVoxSynthParams& params) {
    return Synthesize(text, speakerId, &params);
}

bool VoiceVoxClient::PlayWavData(const std::vector<uint8_t>& wavData) {
    return PlayWav(wavData);
}

std::future<bool> VoiceVoxClient::PlayWavDataAsync(std::vector<uint8_t> wavData) {
    return std::async(std::launch::async, [this, data = std::move(wavData)]() {
        return PlayWav(data);
    });
}

std::vector<uint8_t> VoiceVoxClient::Synthesize(const std::string& text, int speakerId, const VoiceVoxSynthParams* params) {
    // Step 1: audio_query
    std::string queryUrl = baseUrl_ + "/audio_query?text=" + UrlEncode(text)
                         + "&speaker=" + std::to_string(speakerId);
    std::string queryJson = HttpPost(queryUrl, "", "application/json");
    if (queryJson.empty()) return {};

    // Step 1.5: apply emotion/synth params
    if (params) {
        ApplySynthParams(queryJson, *params);
    }

    // Step 2: synthesis
    std::string synthUrl = baseUrl_ + "/synthesis?speaker=" + std::to_string(speakerId);

    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::vector<uint8_t> wavData;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, synthUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, queryJson.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(queryJson.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBinaryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wavData);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return {};
    return wavData;
}

bool VoiceVoxClient::PlayWav(const std::vector<uint8_t>& wavData) {
    if (!xaudio2_ || !masterVoice_) return false;

    WavHeader header{};
    if (!ParseWavHeader(wavData, header)) return false;

    if (sourceVoice_) {
        sourceVoice_->Stop();
        sourceVoice_->FlushSourceBuffers();
        sourceVoice_->DestroyVoice();
        sourceVoice_ = nullptr;
    }

    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = header.channels;
    wfx.nSamplesPerSec = header.sampleRate;
    wfx.wBitsPerSample = header.bitsPerSample;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HRESULT hr = xaudio2_->CreateSourceVoice(&sourceVoice_, &wfx);
    if (FAILED(hr)) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        playbackBuffer_.assign(
            wavData.begin() + header.dataOffset,
            wavData.begin() + header.dataOffset + header.dataSize);
    }

    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = static_cast<UINT32>(playbackBuffer_.size());
    buf.pAudioData = playbackBuffer_.data();
    buf.Flags = XAUDIO2_END_OF_STREAM;

    hr = sourceVoice_->SubmitSourceBuffer(&buf);
    if (FAILED(hr)) return false;

    isPlaying_.store(true);
    sourceVoice_->Start();

    // Wait for playback to finish
    XAUDIO2_VOICE_STATE state{};
    do {
        sourceVoice_->GetState(&state);
        if (state.BuffersQueued == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (true);

    isPlaying_.store(false);
    return true;
}

bool VoiceVoxClient::IsPlaying() const {
    return isPlaying_.load();
}

void VoiceVoxClient::Stop() {
    if (sourceVoice_) {
        sourceVoice_->Stop();
        sourceVoice_->FlushSourceBuffers();
    }
    isPlaying_.store(false);
}

std::string VoiceVoxClient::HttpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? response : "";
}

std::string VoiceVoxClient::HttpPost(const std::string& url, const std::string& body, const std::string& contentType) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    struct curl_slist* headers = nullptr;
    if (!contentType.empty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + contentType).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK) ? response : "";
}

void VoiceVoxClient::ApplySynthParams(std::string& json, const VoiceVoxSynthParams& params) {
    auto readFloat = [&](const std::string& key) -> float {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return 0.0f;
        pos += search.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        try {
            return std::stof(json.substr(pos));
        } catch (...) {
            return 0.0f;
        }
    };

    auto writeFloat = [&](const std::string& key, float value) {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return;
        pos += search.size();
        while (pos < json.size() && json[pos] == ' ') ++pos;
        auto end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", value);
        json.replace(pos, end - pos, buf);
    };

    writeFloat("speedScale", readFloat("speedScale") * params.speedScale);
    writeFloat("pitchScale", readFloat("pitchScale") + params.pitchScale);
    writeFloat("intonationScale", readFloat("intonationScale") * params.intonationScale);
    writeFloat("volumeScale", readFloat("volumeScale") * params.volumeScale);

    if (params.pauseScale != 1.0f) {
        size_t searchFrom = 0;
        while (true) {
            auto pausePos = json.find("\"pause_mora\":{", searchFrom);
            if (pausePos == std::string::npos) break;

            auto blockEnd = json.find("}", pausePos + 14);
            if (blockEnd == std::string::npos) break;

            auto vlKey = json.find("\"vowel_length\":", pausePos);
            if (vlKey != std::string::npos && vlKey < blockEnd) {
                size_t valStart = vlKey + 15;
                while (valStart < json.size() && json[valStart] == ' ') ++valStart;
                size_t valEnd = valStart;
                while (valEnd < json.size() && json[valEnd] != ',' && json[valEnd] != '}') ++valEnd;

                try {
                    float cur = std::stof(json.substr(valStart, valEnd - valStart));
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.4f", cur * params.pauseScale);
                    std::string rep(buf);
                    json.replace(valStart, valEnd - valStart, rep);
                    searchFrom = valStart + rep.size();
                } catch (...) {
                    searchFrom = blockEnd + 1;
                }
            } else {
                searchFrom = blockEnd + 1;
            }
        }
    }
}
