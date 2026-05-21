#include "LLMClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstring>
#include <sstream>

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

struct StreamContext {
    LLMStreamCallback callback;
    std::string buffer;
    std::string accumulated;
    std::atomic<bool>* cancelFlag;
};

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb, StreamContext* ctx) {
    if (ctx->cancelFlag && ctx->cancelFlag->load()) {
        return 0;
    }

    size_t totalSize = size * nmemb;
    ctx->buffer.append(static_cast<char*>(contents), totalSize);

    size_t pos = 0;
    while (true) {
        size_t newline = ctx->buffer.find('\n', pos);
        if (newline == std::string::npos) break;

        std::string line = ctx->buffer.substr(pos, newline - pos);
        pos = newline + 1;

        if (line.empty() || line == "\r") continue;

        if (line.back() == '\r') {
            line.pop_back();
        }

        if (line.rfind("data: ", 0) == 0) {
            std::string data = line.substr(6);

            if (data == "[DONE]") {
                if (ctx->callback) {
                    ctx->callback("", true);
                }
                continue;
            }

            // content_block_delta イベントからテキストを抽出
            const std::string textKey = "\"text\":\"";
            auto textPos = data.find(textKey);
            if (textPos != std::string::npos) {
                // "type":"content_block_delta" を含むか確認
                if (data.find("\"content_block_delta\"") != std::string::npos) {
                    textPos += textKey.size();
                    std::string text;
                    while (textPos < data.size() && data[textPos] != '"') {
                        if (data[textPos] == '\\' && textPos + 1 < data.size()) {
                            ++textPos;
                            switch (data[textPos]) {
                            case 'n': text += '\n'; break;
                            case 't': text += '\t'; break;
                            case '"': text += '"'; break;
                            case '\\': text += '\\'; break;
                            case '/': text += '/'; break;
                            case 'u': {
                                if (textPos + 4 < data.size()) {
                                    std::string hex = data.substr(textPos + 1, 4);
                                    unsigned int codepoint = std::stoul(hex, nullptr, 16);
                                    textPos += 4;
                                    if (codepoint <= 0x7F) {
                                        text += static_cast<char>(codepoint);
                                    } else if (codepoint <= 0x7FF) {
                                        text += static_cast<char>(0xC0 | (codepoint >> 6));
                                        text += static_cast<char>(0x80 | (codepoint & 0x3F));
                                    } else {
                                        text += static_cast<char>(0xE0 | (codepoint >> 12));
                                        text += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                                        text += static_cast<char>(0x80 | (codepoint & 0x3F));
                                    }
                                }
                                break;
                            }
                            default: text += data[textPos]; break;
                            }
                        } else {
                            text += data[textPos];
                        }
                        ++textPos;
                    }
                    ctx->accumulated += text;
                    if (ctx->callback) {
                        ctx->callback(text, false);
                    }
                }
            }

            // message_stop イベント
            if (data.find("\"message_stop\"") != std::string::npos) {
                if (ctx->callback) {
                    ctx->callback("", true);
                }
            }
        }
    }

    ctx->buffer = ctx->buffer.substr(pos);
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

} // namespace

LLMClient::LLMClient() {}

LLMClient::~LLMClient() {}

void LLMClient::SetApiKey(const std::string& apiKey) {
    std::lock_guard<std::mutex> lock(mutex_);
    apiKey_ = apiKey;
}

void LLMClient::SetModel(const std::string& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_ = model;
}

void LLMClient::SetSystemPrompt(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(mutex_);
    systemPrompt_ = prompt;
}

void LLMClient::SetMaxTokens(int maxTokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxTokens_ = maxTokens;
}

void LLMClient::AddMessage(const std::string& role, const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    LLMMessage msg;
    msg.role = role;
    msg.content = content;
    history_.push_back(std::move(msg));
}

void LLMClient::AddMessageWithImage(const std::string& role, const std::string& text,
                                     const uint8_t* bgraData, uint32_t width, uint32_t height) {
    std::string base64 = EncodeToBase64Jpeg(bgraData, width, height);
    if (base64.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    LLMMessage msg;
    msg.role = role;
    msg.content = text;
    LLMMessage::ImageContent img;
    img.base64Data = std::move(base64);
    msg.images.push_back(std::move(img));
    history_.push_back(std::move(msg));
}

void LLMClient::AddMessageWithImages(const std::string& role, const std::string& text,
                                      const std::vector<ImageFrame>& frames) {
    LLMMessage msg;
    msg.role = role;
    msg.content = text;
    for (auto& frame : frames) {
        std::string base64 = EncodeToBase64Jpeg(frame.data, frame.width, frame.height);
        if (base64.empty()) continue;
        LLMMessage::ImageContent img;
        img.base64Data = std::move(base64);
        msg.images.push_back(std::move(img));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    history_.push_back(std::move(msg));
}

void LLMClient::ClearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.clear();
}

LLMResponse LLMClient::SendBlocking() {
    isProcessing_.store(true);
    cancelRequested_.store(false);
    LLMResponse result = SendRequest();
    if (result.success) {
        std::lock_guard<std::mutex> lock(mutex_);
        LLMMessage assistantMsg;
        assistantMsg.role = "assistant";
        assistantMsg.content = result.content;
        history_.push_back(std::move(assistantMsg));
    }
    isProcessing_.store(false);
    return result;
}

std::future<LLMResponse> LLMClient::SendAsync() {
    return std::async(std::launch::async, [this]() {
        return SendBlocking();
    });
}

std::future<LLMResponse> LLMClient::SendStreamAsync(LLMStreamCallback callback) {
    return std::async(std::launch::async, [this, cb = std::move(callback)]() {
        isProcessing_.store(true);
        cancelRequested_.store(false);
        LLMResponse result = SendStreamRequest(cb);
        if (result.success) {
            std::lock_guard<std::mutex> lock(mutex_);
            LLMMessage assistantMsg;
            assistantMsg.role = "assistant";
            assistantMsg.content = result.content;
            history_.push_back(std::move(assistantMsg));
        }
        isProcessing_.store(false);
        return result;
    });
}

void LLMClient::Cancel() {
    cancelRequested_.store(true);
}

std::string LLMClient::BuildRequestBody() const {
    std::ostringstream body;
    body << R"({"model":")" << model_ << R"(",)";
    body << R"("max_tokens":)" << maxTokens_ << ",";

    if (!systemPrompt_.empty()) {
        body << R"("system":")" << EscapeJson(systemPrompt_) << R"(",)";
    }

    body << R"("messages":[)";
    for (size_t i = 0; i < history_.size(); ++i) {
        const auto& msg = history_[i];
        if (i > 0) body << ",";

        body << R"({"role":")" << msg.role << R"(","content":)";

        if (msg.images.empty()) {
            body << R"(")" << EscapeJson(msg.content) << R"(")";
        } else {
            body << "[";
            for (size_t j = 0; j < msg.images.size(); ++j) {
                if (j > 0) body << ",";
                body << R"({"type":"image","source":{"type":"base64","media_type":")"
                     << msg.images[j].mediaType << R"(","data":")"
                     << msg.images[j].base64Data << R"("}})";
            }
            if (!msg.content.empty()) {
                body << R"(,{"type":"text","text":")" << EscapeJson(msg.content) << R"("})";
            }
            body << "]";
        }
        body << "}";
    }
    body << "]}";

    return body.str();
}

std::string LLMClient::EncodeToBase64Jpeg(const uint8_t* bgraData, uint32_t width, uint32_t height) {
    std::vector<uint8_t> rgb(width * height * 3);
    for (uint32_t i = 0; i < width * height; ++i) {
        rgb[i * 3 + 0] = bgraData[i * 4 + 2];
        rgb[i * 3 + 1] = bgraData[i * 4 + 1];
        rgb[i * 3 + 2] = bgraData[i * 4 + 0];
    }

    JpegWriteContext ctx;
    int ok = stbi_write_jpg_to_func(JpegWriteFunc, &ctx, static_cast<int>(width), static_cast<int>(height), 3, rgb.data(), 85);
    if (!ok || ctx.buffer.empty()) return "";

    return Base64Encode(ctx.buffer.data(), ctx.buffer.size());
}

LLMResponse LLMClient::SendRequest() {
    LLMResponse result;

    std::string apiKey, bodyStr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apiKey = apiKey_;
        bodyStr = BuildRequestBody();
    }

    if (apiKey.empty()) {
        result.error = "API key not set";
        return result;
    }

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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        result.error = std::string("curl error: ") + curl_easy_strerror(res);
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        if (httpCode == 200) {
            result.content = ParseResponseContent(response);
            ParseUsage(response, result.inputTokens, result.outputTokens);
            result.success = true;
        } else {
            result.error = "HTTP " + std::to_string(httpCode) + ": " + ParseErrorMessage(response);
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

LLMResponse LLMClient::SendStreamRequest(LLMStreamCallback callback) {
    LLMResponse result;

    std::string apiKey, bodyStr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apiKey = apiKey_;

        std::ostringstream body;
        body << R"({"model":")" << model_ << R"(",)";
        body << R"("max_tokens":)" << maxTokens_ << ",";
        body << R"("stream":true,)";

        if (!systemPrompt_.empty()) {
            body << R"("system":")" << EscapeJson(systemPrompt_) << R"(",)";
        }

        body << R"("messages":[)";
        for (size_t i = 0; i < history_.size(); ++i) {
            const auto& msg = history_[i];
            if (i > 0) body << ",";

            body << R"({"role":")" << msg.role << R"(","content":)";

            if (msg.images.empty()) {
                body << R"(")" << EscapeJson(msg.content) << R"(")";
            } else {
                body << "[";
                for (size_t j = 0; j < msg.images.size(); ++j) {
                    if (j > 0) body << ",";
                    body << R"({"type":"image","source":{"type":"base64","media_type":")"
                         << msg.images[j].mediaType << R"(","data":")"
                         << msg.images[j].base64Data << R"("}})";
                }
                if (!msg.content.empty()) {
                    body << R"(,{"type":"text","text":")" << EscapeJson(msg.content) << R"("})";
                }
                body << "]";
            }
            body << "}";
        }
        body << "]}";
        bodyStr = body.str();
    }

    if (apiKey.empty()) {
        result.error = "API key not set";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to initialize curl";
        return result;
    }

    StreamContext streamCtx;
    streamCtx.callback = callback;
    streamCtx.cancelFlag = &cancelRequested_;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-api-key: " + apiKey).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(bodyStr.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &streamCtx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    if (cancelRequested_.load()) {
        result.error = "Cancelled";
    } else if (res != CURLE_OK) {
        result.error = std::string("curl error: ") + curl_easy_strerror(res);
    } else {
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        if (httpCode == 200) {
            result.content = streamCtx.accumulated;
            result.success = true;
        } else {
            result.error = "HTTP " + std::to_string(httpCode) + ": " + streamCtx.buffer;
        }
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

std::string LLMClient::EscapeJson(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                output += buf;
            } else {
                output += c;
            }
            break;
        }
    }
    return output;
}

std::string LLMClient::ParseResponseContent(const std::string& json) {
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
            case 'u': {
                if (pos + 4 < json.size()) {
                    std::string hex = json.substr(pos + 1, 4);
                    unsigned int codepoint = std::stoul(hex, nullptr, 16);
                    pos += 4;
                    if (codepoint <= 0x7F) {
                        result += static_cast<char>(codepoint);
                    } else if (codepoint <= 0x7FF) {
                        result += static_cast<char>(0xC0 | (codepoint >> 6));
                        result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | (codepoint >> 12));
                        result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                }
                break;
            }
            default: result += json[pos]; break;
            }
        } else {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

std::string LLMClient::ParseErrorMessage(const std::string& json) {
    const std::string key = "\"message\":\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return json;
    pos += key.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return json;
    return json.substr(pos, end - pos);
}

void LLMClient::ParseUsage(const std::string& json, int& inputTokens, int& outputTokens) {
    auto extractInt = [&](const std::string& key) -> int {
        auto pos = json.find("\"" + key + "\":");
        if (pos == std::string::npos) return 0;
        pos += key.size() + 3;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        std::string numStr;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
            numStr += json[pos++];
        }
        return numStr.empty() ? 0 : std::stoi(numStr);
    };
    inputTokens = extractInt("input_tokens");
    outputTokens = extractInt("output_tokens");
}
