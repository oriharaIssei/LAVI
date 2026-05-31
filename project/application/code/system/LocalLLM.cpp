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
    cachedTokens_.clear();
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

    // 同期生成（記憶系の要約等）は会話とは別プロンプトなので、
    // KV を全クリアし、会話側のプレフィックスキャッシュも無効化する
    llama_memory_clear(llama_get_memory(ctx_), true);
    cachedTokens_.clear();

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
    return stripThink_ ? StripThink(result) : result;
}

bool LocalLLM::HasChatTemplate() const {
    if (!model_) return false;
    const char* tmpl = llama_model_chat_template(model_, nullptr);
    return tmpl != nullptr && tmpl[0] != '\0';
}

std::string LocalLLM::GenerateChat(const std::string& systemPrompt, const std::string& userPrompt) {
    if (!model_) return Generate(userPrompt);

    const std::string sys = ApplyNoThink(systemPrompt);

    const char* tmpl = llama_model_chat_template(model_, nullptr);
    if (!tmpl || tmpl[0] == '\0') {
        // chat template なし → raw 生成にフォールバック
        std::string combined;
        if (!sys.empty()) {
            combined = sys + "\n\n" + userPrompt;
        } else {
            combined = userPrompt;
        }
        return Generate(combined);
    }

    // llama_chat_apply_template でフォーマット
    std::vector<llama_chat_message> messages;
    llama_chat_message sysMsg{};
    sysMsg.role = "system";
    sysMsg.content = sys.c_str();
    llama_chat_message userMsg{};
    userMsg.role = "user";
    userMsg.content = userPrompt.c_str();

    if (!sys.empty()) messages.push_back(sysMsg);
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

std::string LocalLLM::ApplyNoThink(const std::string& systemPrompt) const {
    if (!disableThinking_) return systemPrompt;
    // Qwen3 等は /no_think で思考を無効化できる
    if (systemPrompt.empty()) return "/no_think";
    return systemPrompt + "\n\n/no_think";
}

std::string LocalLLM::StripThink(const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t open = text.find("<think>", i);
        if (open == std::string::npos) {
            out += text.substr(i);
            break;
        }
        out += text.substr(i, open - i);
        size_t close = text.find("</think>", open);
        if (close == std::string::npos) {
            break; // 未閉じの think は以降を破棄
        }
        i = close + 8; // "</think>" の長さ
    }
    // 思考除去後に残る先頭の空白・改行を整理
    size_t b = out.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return out.substr(b);
}

std::string LocalLLM::FormatChat(const std::string& systemPrompt,
                                 const std::vector<LocalChatMessage>& messages) const {
    const std::string sys = ApplyNoThink(systemPrompt);

    auto concat = [&]() {
        std::string combined;
        if (!sys.empty()) combined = sys + "\n\n";
        for (const auto& m : messages) {
            combined += m.role + ": " + m.content + "\n";
        }
        combined += "assistant: ";
        return combined;
    };

    if (!model_) return concat();

    const char* tmpl = llama_model_chat_template(model_, nullptr);
    if (!tmpl || tmpl[0] == '\0') {
        return concat();
    }

    std::vector<llama_chat_message> chat;
    if (!sys.empty()) {
        chat.push_back({"system", sys.c_str()});
    }
    for (const auto& m : messages) {
        chat.push_back({m.role.c_str(), m.content.c_str()});
    }

    std::vector<char> buf(static_cast<size_t>(contextSize_) * 4);
    int len = llama_chat_apply_template(
        tmpl, chat.data(), static_cast<int32_t>(chat.size()),
        true, buf.data(), static_cast<int32_t>(buf.size()));

    if (len < 0 || len > static_cast<int>(buf.size())) {
        return concat();
    }
    return std::string(buf.data(), len);
}

std::future<std::string> LocalLLM::GenerateChatStreamAsync(const std::string& systemPrompt,
                                                           const std::vector<LocalChatMessage>& messages,
                                                           LocalLLMCallback callback) {
    // テンプレート整形は呼び出しスレッドで行い、生成は既存のストリーミング経路へ委譲
    std::string formatted = FormatChat(systemPrompt, messages);
    return GenerateAsync(formatted, std::move(callback));
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
            cachedTokens_.clear();
            isProcessing_.store(false);
            return std::string();
        }
        tokens.resize(nTokens);

        // 前回トークン列との共通プレフィックスを KV に残し、新規部分のみ再デコードする。
        // ペルソナ等の先頭固定部分は再処理されない（プロンプトキャッシュ）。
        llama_memory_t mem = llama_get_memory(ctx_);
        int nKeep = 0;
        int maxKeep = (std::min)(static_cast<int>(cachedTokens_.size()), nTokens - 1);
        if (maxKeep < 0) maxKeep = 0;
        while (nKeep < maxKeep && cachedTokens_[nKeep] == tokens[nKeep]) {
            ++nKeep;
        }
        if (nKeep > 0) {
            llama_memory_seq_rm(mem, 0, nKeep, -1); // 共通プレフィックス以降を破棄
        } else {
            llama_memory_clear(mem, true);
        }

        const int nBatch = 512;
        bool decodeFailed = false;
        for (int start = nKeep; start < nTokens; start += nBatch) {
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
            cachedTokens_.clear();
            isProcessing_.store(false);
            return std::string();
        }

        std::string result;
        llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.3f));
        llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
        llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));

        // <think>...</think> をストリーミングで除去してから callback へ流す
        bool inThink = false;
        std::string hold;
        auto safeLen = [](const std::string& b, const std::string& tag) -> size_t {
            size_t safe = b.size();
            for (size_t n = 1; n < tag.size() && n <= b.size(); ++n) {
                if (b.compare(b.size() - n, n, tag, 0, n) == 0) { safe = b.size() - n; break; }
            }
            return safe;
        };
        auto emitClean = [&](const std::string& piece) {
            if (!stripThink_) { if (cb) cb(piece); return; }
            hold += piece;
            while (!hold.empty()) {
                if (!inThink) {
                    size_t open = hold.find("<think>");
                    if (open == std::string::npos) {
                        size_t safe = safeLen(hold, "<think>");
                        if (safe > 0) { if (cb) cb(hold.substr(0, safe)); hold.erase(0, safe); }
                        break;
                    }
                    if (open > 0 && cb) cb(hold.substr(0, open));
                    hold.erase(0, open + 7); // "<think>"
                    inThink = true;
                } else {
                    size_t close = hold.find("</think>");
                    if (close == std::string::npos) {
                        size_t safe = safeLen(hold, "</think>");
                        hold.erase(0, safe); // think 内は破棄
                        break;
                    }
                    hold.erase(0, close + 8); // "</think>"
                    inThink = false;
                }
            }
        };

        // KV に入っている内容（プロンプト）をキャッシュとして記録。以降、生成トークンも追記する
        cachedTokens_.assign(tokens.begin(), tokens.end());

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
                emitClean(piece);
            }

            llama_batch single = llama_batch_init(1, 0, 1);
            BatchAdd(single, newToken, curPos, true);
            if (llama_decode(ctx_, single) != 0) {
                llama_batch_free(single);
                break;
            }
            llama_batch_free(single);
            ++curPos;
            cachedTokens_.push_back(newToken); // KV に入った生成トークンを追記
        }

        // think 外で保留している残りを出し切る
        if (stripThink_ && !inThink && !hold.empty() && cb) cb(hold);

        llama_sampler_free(sampler);
        isProcessing_.store(false);
        return stripThink_ ? StripThink(result) : result;
    });
}

void LocalLLM::Cancel() {
    cancelRequested_.store(true);
}
