#include "LocalLLM.h"

#include <llama.h>

#include <cstring>
#include <vector>

namespace {

void BatchAdd(llama_batch& batch, llama_token token, llama_pos pos, bool logits) {
    int32_t i = batch.n_tokens;
    batch.token[i] = token;
    batch.pos[i] = pos;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    batch.logits[i] = logits ? 1 : 0;
    batch.n_tokens++;
}

} // namespace

LocalLLM::LocalLLM() {}

LocalLLM::~LocalLLM() {
    UnloadModel();
}

bool LocalLLM::LoadModel(const std::string& modelPath, int nGpuLayers, int contextSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    UnloadModel();

    contextSize_ = contextSize;

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = nGpuLayers;

    model_ = llama_model_load_from_file(modelPath.c_str(), modelParams);
    if (!model_) {
        return false;
    }

    llama_context_params ctxParams = llama_context_default_params();
    ctxParams.n_ctx = static_cast<uint32_t>(contextSize_);
    ctxParams.n_batch = 512;

    ctx_ = llama_init_from_model(model_, ctxParams);
    if (!ctx_) {
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    return true;
}

void LocalLLM::UnloadModel() {
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
}

bool LocalLLM::IsModelLoaded() const {
    return model_ != nullptr && ctx_ != nullptr;
}

void LocalLLM::SetMaxTokens(int maxTokens) {
    maxTokens_ = maxTokens;
}

std::string LocalLLM::Generate(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!model_ || !ctx_) return "";

    isProcessing_.store(true);
    cancelRequested_.store(false);

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    std::vector<llama_token> tokens(contextSize_);
    int nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                  tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
    if (nTokens < 0) {
        isProcessing_.store(false);
        return "";
    }
    tokens.resize(nTokens);

    llama_memory_clear(llama_get_memory(ctx_), true);

    const int nBatch = 512;
    for (int start = 0; start < nTokens; start += nBatch) {
        int end = (std::min)(start + nBatch, nTokens);
        int chunkSize = end - start;
        llama_batch batch = llama_batch_init(chunkSize, 0, 1);
        for (int i = start; i < end; ++i) {
            BatchAdd(batch, tokens[i], i, (i == nTokens - 1));
        }
        int ret = llama_decode(ctx_, batch);
        llama_batch_free(batch);
        if (ret != 0) {
            isProcessing_.store(false);
            return "";
        }
    }

    std::string result;
    llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.3f));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));

    int curPos = nTokens;
    for (int i = 0; i < maxTokens_; ++i) {
        if (cancelRequested_.load()) break;

        llama_token newToken = llama_sampler_sample(sampler, ctx_, -1);

        if (llama_vocab_is_eog(vocab, newToken)) break;

        char buf[128];
        int len = llama_token_to_piece(vocab, newToken, buf, sizeof(buf), 0, true);
        if (len > 0) {
            result.append(buf, len);
        }

        llama_batch single = llama_batch_init(1, 0, 1);
        BatchAdd(single, newToken, curPos, true);
        if (llama_decode(ctx_, single) != 0) {
            llama_batch_free(single);
            break;
        }
        llama_batch_free(single);
        ++curPos;
    }

    llama_sampler_free(sampler);
    isProcessing_.store(false);
    return result;
}

bool LocalLLM::HasChatTemplate() const {
    if (!model_) return false;
    const char* tmpl = llama_model_chat_template(model_, nullptr);
    return tmpl != nullptr && tmpl[0] != '\0';
}

std::string LocalLLM::GenerateChat(const std::string& systemPrompt, const std::string& userPrompt) {
    if (!model_) return Generate(userPrompt);

    const char* tmpl = llama_model_chat_template(model_, nullptr);
    if (!tmpl || tmpl[0] == '\0') {
        // chat template なし → raw 生成にフォールバック
        std::string combined;
        if (!systemPrompt.empty()) {
            combined = systemPrompt + "\n\n" + userPrompt;
        } else {
            combined = userPrompt;
        }
        return Generate(combined);
    }

    // llama_chat_apply_template でフォーマット
    std::vector<llama_chat_message> messages;
    llama_chat_message sysMsg{};
    sysMsg.role = "system";
    sysMsg.content = systemPrompt.c_str();
    llama_chat_message userMsg{};
    userMsg.role = "user";
    userMsg.content = userPrompt.c_str();

    if (!systemPrompt.empty()) messages.push_back(sysMsg);
    messages.push_back(userMsg);

    std::vector<char> buf(contextSize_ * 4);
    int len = llama_chat_apply_template(
        tmpl, messages.data(), static_cast<int32_t>(messages.size()),
        true, buf.data(), static_cast<int32_t>(buf.size()));

    if (len < 0 || len > static_cast<int>(buf.size())) {
        return Generate(systemPrompt.empty() ? userPrompt : systemPrompt + "\n\n" + userPrompt);
    }

    std::string formatted(buf.data(), len);
    return Generate(formatted);
}

std::future<std::string> LocalLLM::GenerateAsync(const std::string& prompt) {
    return std::async(std::launch::async, [this, prompt]() {
        return Generate(prompt);
    });
}

std::future<std::string> LocalLLM::GenerateAsync(const std::string& prompt, LocalLLMCallback callback) {
    return std::async(std::launch::async, [this, prompt, cb = std::move(callback)]() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!model_ || !ctx_) return std::string();

        isProcessing_.store(true);
        cancelRequested_.store(false);

        const llama_vocab* vocab = llama_model_get_vocab(model_);

        std::vector<llama_token> tokens(contextSize_);
        int nTokens = llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                                      tokens.data(), static_cast<int32_t>(tokens.size()), true, true);
        if (nTokens < 0) {
            isProcessing_.store(false);
            return std::string();
        }
        tokens.resize(nTokens);

        llama_memory_clear(llama_get_memory(ctx_), true);

        const int nBatch = 512;
        bool decodeFailed = false;
        for (int start = 0; start < nTokens; start += nBatch) {
            int end = (std::min)(start + nBatch, nTokens);
            int chunkSize = end - start;
            llama_batch batch = llama_batch_init(chunkSize, 0, 1);
            for (int i = start; i < end; ++i) {
                BatchAdd(batch, tokens[i], i, (i == nTokens - 1));
            }
            int ret = llama_decode(ctx_, batch);
            llama_batch_free(batch);
            if (ret != 0) { decodeFailed = true; break; }
        }
        if (decodeFailed) {
            isProcessing_.store(false);
            return std::string();
        }

        std::string result;
        llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.3f));
        llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));

        int curPos = nTokens;
        for (int i = 0; i < maxTokens_; ++i) {
            if (cancelRequested_.load()) break;

            llama_token newToken = llama_sampler_sample(sampler, ctx_, -1);
            if (llama_vocab_is_eog(vocab, newToken)) break;

            char buf[128];
            int len = llama_token_to_piece(vocab, newToken, buf, sizeof(buf), 0, true);
            if (len > 0) {
                std::string piece(buf, len);
                result += piece;
                if (cb) cb(piece);
            }

            llama_batch single = llama_batch_init(1, 0, 1);
            BatchAdd(single, newToken, curPos, true);
            if (llama_decode(ctx_, single) != 0) {
                llama_batch_free(single);
                break;
            }
            llama_batch_free(single);
            ++curPos;
        }

        llama_sampler_free(sampler);
        isProcessing_.store(false);
        return result;
    });
}

void LocalLLM::Cancel() {
    cancelRequested_.store(true);
}
