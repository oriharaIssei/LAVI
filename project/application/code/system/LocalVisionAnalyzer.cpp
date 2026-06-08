#include "LocalVisionAnalyzer.h"

#include <llama.h>
#include "mtmd.h"
#include "mtmd-helper.h"

#include <algorithm>
#include <thread>
#include <vector>

namespace {
// llama_batch に 1 トークン追加（LocalLLM.cpp の BatchAdd と同等）。
void BatchAdd(llama_batch& batch, llama_token token, llama_pos pos, bool logits) {
    int32_t i = batch.n_tokens;
    batch.token[i]    = token;
    batch.pos[i]      = pos;
    batch.n_seq_id[i] = 1;
    batch.seq_id[i][0] = 0;
    batch.logits[i]   = logits ? 1 : 0;
    batch.n_tokens++;
}
// BGRA → RGB（mtmd_bitmap は RGB 連続を期待）。
std::vector<uint8_t> BgraToRgb(const uint8_t* bgra, uint32_t w, uint32_t h) {
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    const size_t px = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < px; ++i) {
        rgb[i * 3 + 0] = bgra[i * 4 + 2]; // R
        rgb[i * 3 + 1] = bgra[i * 4 + 1]; // G
        rgb[i * 3 + 2] = bgra[i * 4 + 0]; // B
    }
    return rgb;
}
} // namespace

LocalVisionAnalyzer::LocalVisionAnalyzer() = default;

LocalVisionAnalyzer::~LocalVisionAnalyzer() {
    UnloadModel();
}

void LocalVisionAnalyzer::SetModelPaths(const std::string& modelPath, const std::string& mmprojPath) {
    modelPath_  = modelPath;
    mmprojPath_ = mmprojPath;
}

bool LocalVisionAnalyzer::IsModelLoaded() const {
    return model_ != nullptr && lctx_ != nullptr && mctx_ != nullptr;
}

bool LocalVisionAnalyzer::LoadModel() {
    std::lock_guard<std::mutex> lock(mutex_);
    UnloadModel();
    if (modelPath_.empty() || mmprojPath_.empty()) {
        return false;
    }

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = gpuLayers_;
    model_ = llama_model_load_from_file(modelPath_.c_str(), mparams);
    if (!model_) {
        return false;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx   = static_cast<uint32_t>(ctxSize_);
    cparams.n_batch = 512;
    lctx_ = llama_init_from_model(model_, cparams);
    if (!lctx_) {
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    mtmd_context_params mp = mtmd_context_params_default();
    mp.use_gpu        = (gpuLayers_ > 0);
    mp.print_timings  = false;
    mp.n_threads      = (std::max)(1u, std::thread::hardware_concurrency() / 2);
    mctx_ = mtmd_init_from_file(mmprojPath_.c_str(), model_, mp);
    if (!mctx_) {
        llama_free(lctx_);
        llama_model_free(model_);
        lctx_ = nullptr;
        model_ = nullptr;
        return false;
    }
    return true;
}

void LocalVisionAnalyzer::UnloadModel() {
    if (mctx_) { mtmd_free(mctx_); mctx_ = nullptr; }
    if (lctx_) { llama_free(lctx_); lctx_ = nullptr; }
    if (model_) { llama_model_free(model_); model_ = nullptr; }
}

// system + (marker 入り user) をチャットテンプレートで整形。テンプレート無ければ素結合。
std::string LocalVisionAnalyzer::FormatPrompt(const std::string& userContentWithMarker) const {
    const char* tmpl = model_ ? llama_model_chat_template(model_, nullptr) : nullptr;
    if (!tmpl || tmpl[0] == '\0') {
        return systemPrompt_.empty() ? userContentWithMarker
                                     : systemPrompt_ + "\n\n" + userContentWithMarker;
    }
    std::vector<llama_chat_message> msgs;
    llama_chat_message sysMsg{"system", systemPrompt_.c_str()};
    llama_chat_message usrMsg{"user", userContentWithMarker.c_str()};
    if (!systemPrompt_.empty()) msgs.push_back(sysMsg);
    msgs.push_back(usrMsg);

    std::vector<char> buf(static_cast<size_t>(ctxSize_) * 4);
    int len = llama_chat_apply_template(tmpl, msgs.data(), static_cast<int32_t>(msgs.size()),
                                        true, buf.data(), static_cast<int32_t>(buf.size()));
    if (len < 0 || len > static_cast<int>(buf.size())) {
        return systemPrompt_.empty() ? userContentWithMarker
                                     : systemPrompt_ + "\n\n" + userContentWithMarker;
    }
    return std::string(buf.data(), len);
}

std::future<VisionResult> LocalVisionAnalyzer::AnalyzeAsync(const uint8_t* bgra, uint32_t width, uint32_t height) {
    return AnalyzeAsync(std::vector<Frame>{Frame{bgra, width, height}});
}

VisionResult LocalVisionAnalyzer::Analyze(const uint8_t* bgra, uint32_t width, uint32_t height) {
    std::vector<RgbFrame> frames;
    if (bgra && width > 0 && height > 0) {
        frames.push_back({BgraToRgb(bgra, width, height), width, height});
    }
    return AnalyzeRgbInternal(std::move(frames));
}

std::future<VisionResult> LocalVisionAnalyzer::AnalyzeAsync(const std::vector<Frame>& frames) {
    // 呼び出し側バッファ参照のため、同期的に RGB へコピーしてから非同期化する。
    std::vector<RgbFrame> rgbs;
    rgbs.reserve(frames.size());
    for (const auto& f : frames) {
        if (!f.bgra || f.width == 0 || f.height == 0) continue;
        rgbs.push_back({BgraToRgb(f.bgra, f.width, f.height), f.width, f.height});
    }
    return std::async(std::launch::async, [this, rgbs = std::move(rgbs)]() mutable -> VisionResult {
        return AnalyzeRgbInternal(std::move(rgbs));
    });
}

VisionResult LocalVisionAnalyzer::AnalyzeRgbInternal(std::vector<RgbFrame> frames) {
    VisionResult out;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!IsModelLoaded()) { out.error = "local vision model not loaded"; return out; }
    if (frames.empty()) { out.error = "no frames"; return out; }

    analyzing_.store(true);
    cancel_.store(false);

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // フレーム枚数ぶん mtmd_bitmap を生成。マーカーも同数ぶん並べる（tokenize が順に画像を挿入）。
    std::vector<mtmd_bitmap*> owned;
    std::vector<const mtmd_bitmap*> bmps;
    owned.reserve(frames.size());
    bmps.reserve(frames.size());
    for (const auto& fr : frames) {
        mtmd_bitmap* b = mtmd_bitmap_init(fr.width, fr.height, fr.rgb.data());
        if (b) { owned.push_back(b); bmps.push_back(b); }
    }
    if (bmps.empty()) { analyzing_.store(false); out.error = "bitmap init failed"; return out; }

    // user 内容 = メディアマーカー×枚数 + プロンプト。
    std::string markers;
    const std::string marker = mtmd_default_marker();
    for (size_t i = 0; i < bmps.size(); ++i) markers += marker + "\n";
    std::string userContent = markers + prompt_;
    std::string formatted = FormatPrompt(userContent);

    mtmd_input_text it{};
    it.text          = formatted.c_str();
    it.add_special   = true;
    it.parse_special = true;

    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    int32_t terr = mtmd_tokenize(mctx_, chunks, &it, bmps.data(), bmps.size());
    if (terr != 0) {
        mtmd_input_chunks_free(chunks);
        for (auto* b : owned) mtmd_bitmap_free(b);
        analyzing_.store(false);
        out.error = "mtmd_tokenize failed";
        return out;
    }

    // 単発推論なので KV を全クリアしてから image+text を評価。
    llama_memory_clear(llama_get_memory(lctx_), true);

    llama_pos newNPast = 0;
    int32_t eerr = mtmd_helper_eval_chunks(mctx_, lctx_, chunks, /*n_past*/ 0, /*seq_id*/ 0,
                                           /*n_batch*/ 512, /*logits_last*/ true, &newNPast);
    mtmd_input_chunks_free(chunks);
    for (auto* b : owned) mtmd_bitmap_free(b);
    if (eerr != 0) {
        analyzing_.store(false);
        out.error = "mtmd eval failed";
        return out;
    }

    // テキスト生成ループ（LocalLLM の sampler と同等の控えめ設定）。
    llama_sampler* sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(sampler, llama_sampler_init_temp(0.3f));
    llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));

    std::string result;
    llama_pos curPos = newNPast;
    for (int i = 0; i < maxTokens_; ++i) {
        if (cancel_.load()) break;
        llama_token tok = llama_sampler_sample(sampler, lctx_, -1);
        if (llama_vocab_is_eog(vocab, tok)) break;

        char piece[256];
        int len = llama_token_to_piece(vocab, tok, piece, sizeof(piece), 0, true);
        if (len > 0) result.append(piece, len);

        llama_batch single = llama_batch_init(1, 0, 1);
        BatchAdd(single, tok, curPos, true);
        int dret = llama_decode(lctx_, single);
        llama_batch_free(single);
        if (dret != 0) break;
        ++curPos;
    }
    llama_sampler_free(sampler);

    analyzing_.store(false);
    out.description = result;
    out.success     = !result.empty();
    if (!out.success && out.error.empty()) out.error = "empty generation";
    return out;
}
