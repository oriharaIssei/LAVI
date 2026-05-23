#include "UserIdentifier.h"
#include "OnnxInferenceEngine.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>

namespace fs = std::filesystem;

namespace {

constexpr int kArcFaceSize = 112;
constexpr int kLbpFaceSize = 64;
constexpr int kLbpGridX = 8;
constexpr int kLbpGridY = 8;
constexpr int kLbpBins = 59;  // uniform LBP patterns

constexpr int kMfccCoeffs = 13;
constexpr int kMfccFrames = 32;
constexpr int kMfccDim = kMfccCoeffs * kMfccFrames;
constexpr int kFftSize = 512;
constexpr int kNumMelFilters = 26;

void Normalize(std::vector<float>& v) {
    float norm = 0.0f;
    for (float x : v) norm += x * x;
    norm = std::sqrt(norm);
    if (norm > 1e-8f) {
        for (float& x : v) x /= norm;
    }
}

// Mel filterbank
float HzToMel(float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); }
float MelToHz(float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); }

std::vector<std::vector<float>> CreateMelFilterbank(int nFilters, int fftSize, int sampleRate) {
    float melLow = HzToMel(0.0f);
    float melHigh = HzToMel(static_cast<float>(sampleRate) / 2.0f);
    std::vector<float> melPoints(nFilters + 2);
    for (int i = 0; i < nFilters + 2; ++i) {
        melPoints[i] = MelToHz(melLow + (melHigh - melLow) * i / (nFilters + 1));
    }

    int nBins = fftSize / 2 + 1;
    std::vector<int> bins(nFilters + 2);
    for (int i = 0; i < nFilters + 2; ++i) {
        bins[i] = static_cast<int>(std::floor((fftSize + 1) * melPoints[i] / sampleRate));
    }

    std::vector<std::vector<float>> filterbank(nFilters, std::vector<float>(nBins, 0.0f));
    for (int m = 0; m < nFilters; ++m) {
        for (int k = bins[m]; k < bins[m + 1] && k < nBins; ++k) {
            filterbank[m][k] = static_cast<float>(k - bins[m]) / (bins[m + 1] - bins[m]);
        }
        for (int k = bins[m + 1]; k < bins[m + 2] && k < nBins; ++k) {
            filterbank[m][k] = static_cast<float>(bins[m + 2] - k) / (bins[m + 2] - bins[m + 1]);
        }
    }
    return filterbank;
}

// Simple DFT magnitude (no dependency on FFTW)
std::vector<float> DftMagnitude(const float* frame, int n) {
    int nBins = n / 2 + 1;
    std::vector<float> mag(nBins);
    for (int k = 0; k < nBins; ++k) {
        float re = 0.0f, im = 0.0f;
        for (int i = 0; i < n; ++i) {
            float angle = 2.0f * 3.14159265f * k * i / n;
            re += frame[i] * std::cos(angle);
            im -= frame[i] * std::sin(angle);
        }
        mag[k] = re * re + im * im;
    }
    return mag;
}

// DCT-II for MFCC
std::vector<float> Dct(const std::vector<float>& input, int nCoeffs) {
    int N = static_cast<int>(input.size());
    std::vector<float> output(nCoeffs);
    for (int k = 0; k < nCoeffs; ++k) {
        float sum = 0.0f;
        for (int n = 0; n < N; ++n) {
            sum += input[n] * std::cos(3.14159265f * k * (2 * n + 1) / (2.0f * N));
        }
        output[k] = sum;
    }
    return output;
}

// Uniform LBP: 58 uniform patterns + 1 non-uniform bin = 59 bins
// Returns the uniform LBP lookup table (256 entries -> bin index)
std::vector<int> BuildUniformLbpTable() {
    std::vector<int> table(256, kLbpBins - 1);  // default: non-uniform bin
    int idx = 0;
    for (int i = 0; i < 256; ++i) {
        uint8_t v = static_cast<uint8_t>(i);
        uint8_t shifted = (v >> 1) | ((v & 1) << 7);
        uint8_t transitions = 0;
        for (int b = 0; b < 8; ++b) {
            if (((v >> b) & 1) != ((shifted >> b) & 1)) ++transitions;
        }
        if (transitions <= 2) {
            table[i] = idx++;
        }
    }
    return table;
}

}  // namespace

UserIdentifier::UserIdentifier()
    : arcface_(std::make_unique<OnnxInferenceEngine>()) {}

UserIdentifier::~UserIdentifier() = default;

bool UserIdentifier::InitializeFace(const std::wstring& modelPath, bool useGpu) {
    return arcface_->LoadModel(modelPath, useGpu);
}

bool UserIdentifier::IsFaceModelLoaded() const {
    return arcface_ && arcface_->IsModelLoaded();
}

std::vector<float> UserIdentifier::ExtractFaceEmbedding(
    const uint8_t* bgra, uint32_t width, uint32_t height,
    int faceX, int faceY, int faceW, int faceH) {

    if (IsFaceModelLoaded()) {
        return ExtractArcFaceEmbedding(bgra, width, height, faceX, faceY, faceW, faceH);
    }
    return ExtractLbpEmbedding(bgra, width, height, faceX, faceY, faceW, faceH);
}

std::vector<float> UserIdentifier::ExtractArcFaceEmbedding(
    const uint8_t* bgra, uint32_t width, uint32_t height,
    int faceX, int faceY, int faceW, int faceH) {

    if (!bgra) return {};

    cv::Mat frame(static_cast<int>(height), static_cast<int>(width), CV_8UC4,
                  const_cast<uint8_t*>(bgra));

    int x = (std::max)(0, faceX);
    int y = (std::max)(0, faceY);
    int w = (std::min)(faceW, static_cast<int>(width) - x);
    int h = (std::min)(faceH, static_cast<int>(height) - y);
    if (w <= 0 || h <= 0) return {};

    cv::Rect roi(x, y, w, h);
    cv::Mat faceRgb;
    cv::cvtColor(frame(roi), faceRgb, cv::COLOR_BGRA2RGB);
    cv::Mat resized;
    cv::resize(faceRgb, resized, cv::Size(kArcFaceSize, kArcFaceSize));

    OnnxTensor input;
    input.shape = {1, 3, kArcFaceSize, kArcFaceSize};
    input.data.resize(3 * kArcFaceSize * kArcFaceSize);

    for (int c = 0; c < 3; ++c) {
        for (int row = 0; row < kArcFaceSize; ++row) {
            const uint8_t* ptr = resized.ptr<uint8_t>(row);
            for (int col = 0; col < kArcFaceSize; ++col) {
                float val = static_cast<float>(ptr[col * 3 + c]);
                input.data[c * kArcFaceSize * kArcFaceSize + row * kArcFaceSize + col] =
                    (val - 127.5f) / 127.5f;
            }
        }
    }

    OnnxTensor output;
    if (!arcface_->Run(input, output)) return {};

    std::vector<float> embedding(output.data.begin(), output.data.end());
    Normalize(embedding);
    return embedding;
}

std::vector<float> UserIdentifier::ExtractLbpEmbedding(
    const uint8_t* bgra, uint32_t width, uint32_t height,
    int faceX, int faceY, int faceW, int faceH) {

    if (!bgra) return {};

    cv::Mat frame(static_cast<int>(height), static_cast<int>(width), CV_8UC4,
                  const_cast<uint8_t*>(bgra));

    int x = (std::max)(0, faceX);
    int y = (std::max)(0, faceY);
    int w = (std::min)(faceW, static_cast<int>(width) - x);
    int h = (std::min)(faceH, static_cast<int>(height) - y);
    if (w <= 0 || h <= 0) return {};

    cv::Rect roi(x, y, w, h);
    cv::Mat gray;
    cv::cvtColor(frame(roi), gray, cv::COLOR_BGRA2GRAY);
    cv::Mat resized;
    cv::resize(gray, resized, cv::Size(kLbpFaceSize, kLbpFaceSize));
    cv::equalizeHist(resized, resized);

    static const std::vector<int> lbpTable = BuildUniformLbpTable();
    cv::Mat lbp(kLbpFaceSize - 2, kLbpFaceSize - 2, CV_8UC1);
    for (int r = 1; r < kLbpFaceSize - 1; ++r) {
        for (int c = 1; c < kLbpFaceSize - 1; ++c) {
            uint8_t center = resized.at<uint8_t>(r, c);
            uint8_t code = 0;
            code |= (resized.at<uint8_t>(r - 1, c - 1) >= center) << 7;
            code |= (resized.at<uint8_t>(r - 1, c    ) >= center) << 6;
            code |= (resized.at<uint8_t>(r - 1, c + 1) >= center) << 5;
            code |= (resized.at<uint8_t>(r    , c + 1) >= center) << 4;
            code |= (resized.at<uint8_t>(r + 1, c + 1) >= center) << 3;
            code |= (resized.at<uint8_t>(r + 1, c    ) >= center) << 2;
            code |= (resized.at<uint8_t>(r + 1, c - 1) >= center) << 1;
            code |= (resized.at<uint8_t>(r    , c - 1) >= center) << 0;
            lbp.at<uint8_t>(r - 1, c - 1) = static_cast<uint8_t>(lbpTable[code]);
        }
    }

    int cellW = (kLbpFaceSize - 2) / kLbpGridX;
    int cellH = (kLbpFaceSize - 2) / kLbpGridY;
    int embDim = kLbpGridX * kLbpGridY * kLbpBins;
    std::vector<float> descriptor(embDim, 0.0f);

    for (int gy = 0; gy < kLbpGridY; ++gy) {
        for (int gx = 0; gx < kLbpGridX; ++gx) {
            int histOffset = (gy * kLbpGridX + gx) * kLbpBins;
            for (int dy = 0; dy < cellH; ++dy) {
                for (int dx = 0; dx < cellW; ++dx) {
                    int bin = lbp.at<uint8_t>(gy * cellH + dy, gx * cellW + dx);
                    descriptor[histOffset + bin] += 1.0f;
                }
            }
        }
    }

    Normalize(descriptor);
    return descriptor;
}

std::vector<float> UserIdentifier::ExtractVoiceFeatures(
    const float* samples, uint32_t sampleCount, uint32_t sampleRate) {

    if (!samples || sampleCount < static_cast<uint32_t>(kFftSize)) return {};

    auto filterbank = CreateMelFilterbank(kNumMelFilters, kFftSize, sampleRate);

    int hopSize = kFftSize / 2;
    int nFrames = static_cast<int>((sampleCount - kFftSize) / hopSize) + 1;
    nFrames = (std::min)(nFrames, kMfccFrames);

    std::vector<float> result;
    result.reserve(kMfccDim);

    // Hann window
    std::vector<float> window(kFftSize);
    for (int i = 0; i < kFftSize; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * i / (kFftSize - 1)));
    }

    std::vector<float> windowed(kFftSize);

    for (int f = 0; f < nFrames; ++f) {
        int offset = f * hopSize;
        for (int i = 0; i < kFftSize; ++i) {
            windowed[i] = samples[offset + i] * window[i];
        }

        auto mag = DftMagnitude(windowed.data(), kFftSize);

        // Mel filterbank
        std::vector<float> melEnergy(kNumMelFilters);
        int nBins = kFftSize / 2 + 1;
        for (int m = 0; m < kNumMelFilters; ++m) {
            float sum = 0.0f;
            for (int k = 0; k < nBins; ++k) {
                sum += mag[k] * filterbank[m][k];
            }
            melEnergy[m] = std::log(sum + 1e-8f);
        }

        auto coeffs = Dct(melEnergy, kMfccCoeffs);
        for (float c : coeffs) {
            result.push_back(c);
        }
    }

    // フレーム数が足りない場合はゼロパディング
    while (static_cast<int>(result.size()) < kMfccDim) {
        result.push_back(0.0f);
    }

    Normalize(result);
    return result;
}

IdentifyResult UserIdentifier::IdentifyFace(const std::vector<float>& embedding) const {
    std::lock_guard<std::mutex> lock(mutex_);
    IdentifyResult best;
    for (int i = 0; i < static_cast<int>(faceProfiles_.size()); ++i) {
        float sim = CosineSimilarity(embedding, faceProfiles_[i].embedding);
        if (sim > best.similarity) {
            best.similarity = sim;
            best.name = faceProfiles_[i].name;
            best.profileIndex = i;
        }
    }
    best.identified = best.similarity >= faceThreshold_;
    return best;
}

IdentifyResult UserIdentifier::IdentifyVoice(const std::vector<float>& mfcc) const {
    std::lock_guard<std::mutex> lock(mutex_);
    IdentifyResult best;
    for (int i = 0; i < static_cast<int>(voiceProfiles_.size()); ++i) {
        float sim = CosineSimilarity(mfcc, voiceProfiles_[i].mfcc);
        if (sim > best.similarity) {
            best.similarity = sim;
            best.name = voiceProfiles_[i].name;
            best.profileIndex = i;
        }
    }
    best.identified = best.similarity >= voiceThreshold_;
    return best;
}

void UserIdentifier::RegisterFace(const std::string& name, const std::vector<float>& embedding) {
    std::lock_guard<std::mutex> lock(mutex_);
    FaceProfile p;
    p.name = name;
    p.embedding = embedding;
    p.sightCount = 1;
    p.lastSeen = GetCurrentTimeString();
    faceProfiles_.push_back(std::move(p));
}

void UserIdentifier::RegisterVoice(const std::string& name, const std::vector<float>& mfcc) {
    std::lock_guard<std::mutex> lock(mutex_);
    VoiceProfile p;
    p.name = name;
    p.mfcc = mfcc;
    p.hearCount = 1;
    p.lastHeard = GetCurrentTimeString();
    voiceProfiles_.push_back(std::move(p));
}

void UserIdentifier::UpdateFaceEmbedding(int index, const std::vector<float>& embedding,
                                          float blendRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(faceProfiles_.size())) return;
    auto& p = faceProfiles_[index];
    for (size_t i = 0; i < p.embedding.size() && i < embedding.size(); ++i) {
        p.embedding[i] = p.embedding[i] * (1.0f - blendRate) + embedding[i] * blendRate;
    }
    Normalize(p.embedding);
    ++p.sightCount;
    p.lastSeen = GetCurrentTimeString();
}

void UserIdentifier::UpdateVoiceMfcc(int index, const std::vector<float>& mfcc,
                                      float blendRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || index >= static_cast<int>(voiceProfiles_.size())) return;
    auto& p = voiceProfiles_[index];
    for (size_t i = 0; i < p.mfcc.size() && i < mfcc.size(); ++i) {
        p.mfcc[i] = p.mfcc[i] * (1.0f - blendRate) + mfcc[i] * blendRate;
    }
    Normalize(p.mfcc);
    ++p.hearCount;
    p.lastHeard = GetCurrentTimeString();
}

float UserIdentifier::CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f, na = 0.0f, nb = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    float denom = std::sqrt(na) * std::sqrt(nb);
    return denom > 1e-8f ? dot / denom : 0.0f;
}

std::string UserIdentifier::GetCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
    return buf;
}

// --- 永続化 (JSON) ---

bool UserIdentifier::Load(const std::string& dirPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string path = dirPath;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';

    auto loadVec = [](const std::string& line, const std::string& key) -> std::vector<float> {
        std::vector<float> v;
        auto pos = line.find("\"" + key + "\":[");
        if (pos == std::string::npos) return v;
        pos = line.find('[', pos);
        auto end = line.find(']', pos);
        if (end == std::string::npos) return v;
        std::string arr = line.substr(pos + 1, end - pos - 1);
        std::istringstream ss(arr);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            v.push_back(std::strtof(tok.c_str(), nullptr));
        }
        return v;
    };

    auto loadStr = [](const std::string& line, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        auto pos = line.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        auto end = line.find('"', pos);
        if (end == std::string::npos) return "";
        return line.substr(pos, end - pos);
    };

    auto loadInt = [](const std::string& line, const std::string& key) -> int {
        std::string search = "\"" + key + "\":";
        auto pos = line.find(search);
        if (pos == std::string::npos) return 0;
        pos += search.size();
        return std::atoi(line.c_str() + pos);
    };

    // Face profiles
    {
        std::ifstream f(path + "face_profiles.json");
        if (f.is_open()) {
            faceProfiles_.clear();
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            size_t pos = 0;
            while (true) {
                auto objStart = content.find('{', pos);
                if (objStart == std::string::npos) break;
                auto objEnd = content.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string obj = content.substr(objStart, objEnd - objStart + 1);

                FaceProfile p;
                p.name = loadStr(obj, "name");
                p.embedding = loadVec(obj, "embedding");
                p.sightCount = loadInt(obj, "sightCount");
                p.lastSeen = loadStr(obj, "lastSeen");
                if (!p.embedding.empty()) {
                    faceProfiles_.push_back(std::move(p));
                }
                pos = objEnd + 1;
            }
        }
    }

    // Voice profiles
    {
        std::ifstream f(path + "voice_profiles.json");
        if (f.is_open()) {
            voiceProfiles_.clear();
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            size_t pos = 0;
            while (true) {
                auto objStart = content.find('{', pos);
                if (objStart == std::string::npos) break;
                auto objEnd = content.find('}', objStart);
                if (objEnd == std::string::npos) break;
                std::string obj = content.substr(objStart, objEnd - objStart + 1);

                VoiceProfile p;
                p.name = loadStr(obj, "name");
                p.mfcc = loadVec(obj, "mfcc");
                p.hearCount = loadInt(obj, "hearCount");
                p.lastHeard = loadStr(obj, "lastHeard");
                if (!p.mfcc.empty()) {
                    voiceProfiles_.push_back(std::move(p));
                }
                pos = objEnd + 1;
            }
        }
    }

    return true;
}

bool UserIdentifier::Save(const std::string& dirPath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string path = dirPath;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';

    fs::create_directories(path);

    auto writeVec = [](std::ostringstream& os, const std::vector<float>& v) {
        os << "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i > 0) os << ",";
            os << v[i];
        }
        os << "]";
    };

    // Face profiles
    {
        std::ostringstream json;
        json << "[\n";
        for (size_t i = 0; i < faceProfiles_.size(); ++i) {
            auto& p = faceProfiles_[i];
            if (i > 0) json << ",\n";
            json << "  {\"name\":\"" << p.name << "\",\"embedding\":";
            writeVec(json, p.embedding);
            json << ",\"sightCount\":" << p.sightCount
                 << ",\"lastSeen\":\"" << p.lastSeen << "\"}";
        }
        json << "\n]\n";
        std::ofstream f(path + "face_profiles.json");
        if (f.is_open()) f << json.str();
    }

    // Voice profiles
    {
        std::ostringstream json;
        json << "[\n";
        for (size_t i = 0; i < voiceProfiles_.size(); ++i) {
            auto& p = voiceProfiles_[i];
            if (i > 0) json << ",\n";
            json << "  {\"name\":\"" << p.name << "\",\"mfcc\":";
            writeVec(json, p.mfcc);
            json << ",\"hearCount\":" << p.hearCount
                 << ",\"lastHeard\":\"" << p.lastHeard << "\"}";
        }
        json << "\n]\n";
        std::ofstream f(path + "voice_profiles.json");
        if (f.is_open()) f << json.str();
    }

    return true;
}
