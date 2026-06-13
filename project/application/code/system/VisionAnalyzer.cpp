#include "VisionAnalyzer.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstring>
#include <sstream>

// stb_image_write for JPEG encoding
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../../externals/stb_image_write.h"

namespace {

static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t octet_a = i < len ? data[i] : 0;
        uint32_t octet_b = (i + 1) < len ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2) < len ? data[i + 2] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result += kBase64Chars[(triple >> 18) & 0x3F];
        result += kBase64Chars[(triple >> 12) & 0x3F];
        result += (i + 1 < len) ? kBase64Chars[(triple >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? kBase64Chars[triple & 0x3F] : '=';
    }
    return result;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

struct JpegWriteContext {
    std::vector<uint8_t> buffer;
};

void JpegWriteFunc(void* context, void* data, int size) {
    auto* ctx = static_cast<JpegWriteContext*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    ctx->buffer.insert(ctx->buffer.end(), bytes, bytes + size);
}

std::string ParseClaudeResponse(const std::string& json) {
    // "text":" の後の値を抽出する簡易パーサー
    const std::string key = "\"text\":\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "";

    pos += key.size();
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
            case 'n': result += '\n'; break;
            case 't': result += '\t'; break;
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

std::string ParseErrorMessage(const std::string& json) {
    const std::string key = "\"message\":\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return json;
    pos += key.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return json;
    return json.substr(pos, end - pos);
}

} // namespace

VisionAnalyzer::VisionAnalyzer() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

VisionAnalyzer::~VisionAnalyzer() {
    curl_global_cleanup();
}

void VisionAnalyzer::SetApiKey(const std::string& apiKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    apiKey_ = apiKey;
}

void VisionAnalyzer::SetModel(const std::string& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_ = model;
}

void VisionAnalyzer::SetPrompt(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(mutex_);
    prompt_ = prompt;
}

std::future<VisionResult> VisionAnalyzer::AnalyzeAsync(const uint8_t* bgraData, uint32_t width, uint32_t height) {
    std::vector<uint8_t> dataCopy(bgraData, bgraData + width * height * 4);
    return std::async(std::launch::async, [this, data = std::move(dataCopy), width, height]() {
        return Analyze(data.data(), width, height);
    });
}

VisionResult VisionAnalyzer::Analyze(const uint8_t* bgraData, uint32_t width, uint32_t height) {
    isAnalyzing_.store(true);
    VisionResult result;

    std::string base64 = EncodeToBase64Jpeg(bgraData, width, height);
    if (base64.empty()) {
        result.error = "Failed to encode image to JPEG";
        isAnalyzing_.store(false);
        return result;
    }

    result = SendRequest(base64);
    isAnalyzing_.store(false);
    return result;
}

std::future<VisionResult> VisionAnalyzer::AnalyzeAsync(const std::vector<Frame>& frames) {
    // 呼び出し側バッファ参照のため、同期的に生バイトをコピーしてから非同期化する。
    struct Owned {
        std::vector<uint8_t> data;
        uint32_t w;
        uint32_t h;
    };
    auto owned = std::make_shared<std::vector<Owned>>();
    owned->reserve(frames.size());
    for (const auto& f : frames) {
        if (!f.bgra || f.width == 0 || f.height == 0) continue;
        owned->push_back({std::vector<uint8_t>(f.bgra, f.bgra + static_cast<size_t>(f.width) * f.height * 4),
                          f.width, f.height});
    }
    return std::async(std::launch::async, [this, owned]() -> VisionResult {
        isAnalyzing_.store(true);
        VisionResult result;
        std::vector<std::string> b64;
        b64.reserve(owned->size());
        for (auto& o : *owned) {
            std::string e = EncodeToBase64Jpeg(o.data.data(), o.w, o.h);
            if (!e.empty()) b64.push_back(std::move(e));
        }
        if (b64.empty()) {
            result.error = "Failed to encode frames to JPEG";
            isAnalyzing_.store(false);
            return result;
        }
        result = SendRequestMulti(b64);
        isAnalyzing_.store(false);
        return result;
    });
}

std::string VisionAnalyzer::EncodeToBase64Jpeg(const uint8_t* bgraData, uint32_t width, uint32_t height) {
    // BGRA -> RGB
    std::vector<uint8_t> rgb(width * height * 3);
    for (uint32_t i = 0; i < width * height; ++i) {
        rgb[i * 3 + 0] = bgraData[i * 4 + 2]; // R
        rgb[i * 3 + 1] = bgraData[i * 4 + 1]; // G
        rgb[i * 3 + 2] = bgraData[i * 4 + 0]; // B
    }

    JpegWriteContext ctx;
    int ok = stbi_write_jpg_to_func(JpegWriteFunc, &ctx, static_cast<int>(width), static_cast<int>(height), 3, rgb.data(), 85);
    if (!ok || ctx.buffer.empty()) return "";

    return Base64Encode(ctx.buffer.data(), ctx.buffer.size());
}

VisionResult VisionAnalyzer::SendRequest(const std::string& base64Image) {
    VisionResult result;

    std::string apiKey, model, prompt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apiKey = apiKey_;
        model = model_;
        prompt = prompt_;
    }

    if (apiKey.empty()) {
        result.error = "API key not set";
        return result;
    }

    // Build JSON request body
    std::ostringstream body;
    body << R"({"model":")" << model << R"(",)";
    body << R"("max_tokens":1024,)";
    body << R"("messages":[{"role":"user","content":[)";
    body << R"({"type":"image","source":{"type":"base64","media_type":"image/jpeg","data":")" << base64Image << R"("}},)";
    body << R"({"type":"text","text":")" << prompt << R"("})";
    body << R"(]}]})";

    std::string bodyStr = body.str();

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to initialize curl";
        return result;
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        result.error = std::string("curl error: ") + curl_easy_strerror(res);
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        if (httpCode == 200) {
            result.description = ParseClaudeResponse(response);
            result.success = true;
        } else {
            result.error = "HTTP " + std::to_string(httpCode) + ": " + ParseErrorMessage(response);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

VisionResult VisionAnalyzer::SendRequestMulti(const std::vector<std::string>& base64Images) {
    VisionResult result;

    std::string apiKey, model, prompt;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apiKey = apiKey_;
        model  = model_;
        prompt = prompt_;
    }

    if (apiKey.empty()) {
        result.error = "API key not set";
        return result;
    }
    if (base64Images.empty()) {
        result.error = "No images";
        return result;
    }

    // JSON 本体を組み立てる。時系列フレームを image ブロックとして並べ、最後に text プロンプト。
    std::ostringstream body;
    body << R"({"model":")" << model << R"(",)";
    body << R"("max_tokens":1024,)";
    body << R"("messages":[{"role":"user","content":[)";
    for (const auto& img : base64Images) {
        body << R"({"type":"image","source":{"type":"base64","media_type":"image/jpeg","data":")" << img << R"("}},)";
    }
    body << R"({"type":"text","text":")" << prompt << R"("})";
    body << R"(]}]})";

    std::string bodyStr = body.str();

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to initialize curl";
        return result;
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); // ワーカースレッドの libcurl はシグナル無効が必須

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        result.error = std::string("curl error: ") + curl_easy_strerror(res);
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        if (httpCode == 200) {
            result.description = ParseClaudeResponse(response);
            result.success     = true;
        } else {
            result.error = "HTTP " + std::to_string(httpCode) + ": " + ParseErrorMessage(response);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}
