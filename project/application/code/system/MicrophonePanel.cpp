#include "MicrophonePanel.h"
#include "SharedMediaContext.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "imgui/imgui.h"
#include "util/StringUtil.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace {
// 改行区切りテキストを語のリストへ（空行・前後空白は除去）
std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        // 前後の空白・CR を除去
        size_t b = line.find_first_not_of(" \t\r\n");
        size_t e = line.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) continue;
        out.push_back(line.substr(b, e - b + 1));
    }
    return out;
}
} // namespace

void MicrophonePanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;

    microphone_ = std::make_unique<OriGine::Microphone>();
    transcriber_ = std::make_unique<WhisperTranscriber>();
    refiner_ = std::make_unique<TranscriptRefiner>();

    micDevices_ = OriGine::Microphone::EnumerateDevices();

    microphone_->SetDataCallback(
        [this](const float* data, uint32_t frameCount, uint32_t channels) {
            float rms = 0.0f;
            uint32_t totalSamples = frameCount * channels;
            for (uint32_t i = 0; i < totalSamples; ++i) {
                rms += data[i] * data[i];
            }
            rms = std::sqrt(rms / static_cast<float>(totalSamples));

            {
                std::lock_guard<std::mutex> lock(audioMutex_);
                currentAudioLevel_ = rms;
                if (rms > peakAudioLevel_) {
                    peakAudioLevel_ = rms;
                }
            }

            if (transcriber_ && transcriber_->IsModelLoaded()) {
                transcriber_->PushAudio(data, frameCount, channels, microphone_->GetFormat().sampleRate);
            }
        });

    // 自己進化ストアを読み込んでから語彙を適用
    correctionMemory_.Load(correctionMemoryPath_);

    // 固有名詞リストを読み込んで transcriber/refiner に適用
    LoadVocabulary();
}

void MicrophonePanel::ApplyEffectiveVocabulary() {
    // ユーザー語彙 ∪ 学習済み固有名詞（重複除去）
    std::vector<std::string> names = SplitLines(vocabularyText_);
    {
        std::unordered_set<std::string> seen(names.begin(), names.end());
        for (const auto& n : correctionMemory_.GetActiveNames(2)) {
            if (seen.insert(n).second) names.push_back(n);
        }
    }
    if (transcriber_) transcriber_->SetVocabulary(names);
    if (refiner_) {
        refiner_->SetVocabulary(names);
        refiner_->SetExamples(correctionMemory_.GetActiveRules(2, 20));
    }
}

void MicrophonePanel::LoadVocabulary() {
    vocabularyText_.clear();
    std::ifstream file(vocabularyPath_);
    if (file.is_open()) {
        try {
            nlohmann::json data;
            file >> data;
            if (data.contains("names") && data["names"].is_array()) {
                for (const auto& n : data["names"]) {
                    if (n.is_string()) {
                        if (!vocabularyText_.empty()) vocabularyText_ += "\n";
                        vocabularyText_ += n.get<std::string>();
                    }
                }
            }
        } catch (...) {
            // パース失敗時は空のまま
        }
    }
    ApplyVocabulary();
}

void MicrophonePanel::ApplyVocabulary() {
    // ユーザー語彙＋学習済みをまとめて反映
    ApplyEffectiveVocabulary();
}

void MicrophonePanel::SaveVocabulary() {
    nlohmann::json data;
    data["names"] = SplitLines(vocabularyText_);
    std::ofstream file(vocabularyPath_);
    if (file.is_open()) {
        file << data.dump(2);
    }
}

void MicrophonePanel::StartRefine(const std::string& rawText) {
    if (!refiner_ || rawText.empty() || ctx_->config.apiKey.empty() || isRefining_) {
        return;
    }
    refiner_->SetApiKey(ctx_->config.apiKey);
    refiner_->SetVocabulary(SplitLines(vocabularyText_));
    // model は未指定（LLMClient 既定の高速モデルを使用）
    refineFuture_ = refiner_->RefineAsync(rawText);
    isRefining_   = true;
}

void MicrophonePanel::Finalize() {
    if (microphone_) {
        microphone_->StopCapture();
        microphone_->Close();
    }
    if (transcriber_) {
        transcriber_->UnloadModel();
    }
    transcriber_.reset();
    microphone_.reset();
}

void MicrophonePanel::Draw() {
    ImGui::Text("Devices: %d", static_cast<int>(micDevices_.size()));
    ImGui::Separator();

    if (!micDevices_.empty()) {
        if (ImGui::BeginCombo("Device", micDevices_[selectedMicDevice_].name.empty()
                                            ? "Unknown"
                                            : ConvertString(micDevices_[selectedMicDevice_].name).c_str())) {
            for (int i = 0; i < static_cast<int>(micDevices_.size()); ++i) {
                if (ImGui::Selectable(ConvertString(micDevices_[i].name).c_str(), selectedMicDevice_ == i)) {
                    selectedMicDevice_ = i;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();

    if (!microphone_->IsCapturing()) {
        if (ImGui::Button("Open & Start Capture")) {
            std::wstring deviceId = micDevices_.empty() ? L"" : micDevices_[selectedMicDevice_].id;
            if (microphone_->Open(deviceId)) {
                microphone_->StartCapture();
            }
        }
    } else {
        if (ImGui::Button("Stop Capture")) {
            microphone_->StopCapture();
            microphone_->Close();
        }

        ImGui::SameLine();

        if (!microphone_->IsRecording()) {
            if (ImGui::Button("Start Recording")) {
                microphone_->StartRecording(recordFilePath_);
            }
        } else {
            if (ImGui::Button("Stop Recording")) {
                microphone_->StopRecording();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        float displayLevel = (std::min)(currentAudioLevel_ * 8.0f, 1.0f);
        ImGui::Text("Level:");
        ImGui::SameLine();
        ImGui::ProgressBar(displayLevel, ImVec2(-1, 0), "");

        ImGui::Text("RMS: %.6f / Peak: %.6f", currentAudioLevel_, peakAudioLevel_);
        if (ImGui::Button("Reset Peak")) {
            peakAudioLevel_ = 0.0f;
        }
    }

    if (microphone_->IsCapturing()) {
        auto& fmt = microphone_->GetFormat();
        ImGui::Text("Format: %uHz / %uch / %ubit", fmt.sampleRate, fmt.channels, fmt.bitsPerSample);
        auto stats = microphone_->GetStats();
        ImGui::Text("Packets: %llu / Frames: %llu / Flags: 0x%08X / LastError: 0x%08X",
                    static_cast<unsigned long long>(stats.packetCount),
                    static_cast<unsigned long long>(stats.frameCount),
                    stats.lastFlags,
                    static_cast<uint32_t>(stats.lastError));
    }

    if (microphone_->IsRecording()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Recording: %s", recordFilePath_.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Speech Recognition (Whisper)");
    ImGui::Separator();

    if (!transcriber_->IsModelLoaded()) {
        ImGui::InputText("Model Path", whisperModelPath_.data(), whisperModelPath_.capacity() + 1,
            ImGuiInputTextFlags_CallbackResize, [](ImGuiInputTextCallbackData* data) -> int {
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    auto* str = static_cast<std::string*>(data->UserData);
                    str->resize(data->BufTextLen);
                    data->Buf = str->data();
                }
                return 0;
            }, &whisperModelPath_);
        if (ImGui::Button("Load Model")) {
            if (transcriber_->LoadModel(whisperModelPath_)) {
                transcriber_->SetVadModelPath(vadModelPath_);
            }
        }
    } else {
        ImGui::Text("Model: Loaded");
        ImGui::SameLine();
        if (ImGui::Button("Unload Model")) {
            transcriber_->UnloadModel();
            transcriber_->ClearAudio();
            ctx_->transcribedText.clear();
        }

        size_t bufferSamples = transcriber_->GetAudioSampleCount();
        float bufferSeconds = bufferSamples / 16000.0f;
        ImGui::Text("Audio Buffer: %zu samples (%.1f sec)", bufferSamples, bufferSeconds);

        if (ImGui::CollapsingHeader("Recognition Settings")) {
            WhisperParams& wp = transcriber_->Params();

            ImGui::Checkbox("Use VAD", &wp.vadEnabled);
            if (!wp.vadEnabled) ImGui::BeginDisabled();
            ImGui::SliderFloat("VAD Threshold", &wp.vadThreshold, 0.05f, 0.95f, "%.2f");
            if (!wp.vadEnabled) ImGui::EndDisabled();
            ImGui::TextDisabled("VAD off = pass all audio to Whisper (diagnose VAD over-gating)");

            ImGui::SliderFloat("Input Gain", &wp.inputGain, 0.1f, 10.0f, "%.2fx");
            ImGui::SliderFloat("Min Avg Prob", &wp.minAvgProb, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox("Filter (sound annotations)", &wp.filterBracketed);

            ImGui::Spacing();
            ImGui::TextDisabled("Advanced (whisper defaults: 0.6 / -1.0 / 2.4 / 0.2)");
            ImGui::SliderFloat("no_speech_thold", &wp.noSpeechThold, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("logprob_thold", &wp.logprobThold, -4.0f, 0.0f, "%.2f");
            ImGui::SliderFloat("entropy_thold", &wp.entropyThold, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("temperature_inc", &wp.temperatureInc, 0.0f, 1.0f, "%.2f");
        }

        if (ImGui::CollapsingHeader("Proper Nouns (Vocabulary)")) {
            ImGui::TextDisabled("1 行 1 語。人名・グループ名などを登録すると綴りが寄ります");
            ImGui::InputTextMultiline("##vocab", vocabularyText_.data(), vocabularyText_.capacity() + 1,
                ImVec2(-1, 120), ImGuiInputTextFlags_CallbackResize,
                [](ImGuiInputTextCallbackData* data) -> int {
                    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                        auto* str = static_cast<std::string*>(data->UserData);
                        str->resize(data->BufTextLen);
                        data->Buf = str->data();
                    }
                    return 0;
                }, &vocabularyText_);

            if (ImGui::Button("Apply##vocab")) ApplyVocabulary();
            ImGui::SameLine();
            if (ImGui::Button("Save##vocab")) { ApplyVocabulary(); SaveVocabulary(); }
            ImGui::SameLine();
            if (ImGui::Button("Reload##vocab")) LoadVocabulary();
            ImGui::SameLine();
            ImGui::TextDisabled("(%d words)", static_cast<int>(SplitLines(vocabularyText_).size()));
        }

        if (ImGui::CollapsingHeader("Self-learning (校正の自己進化)")) {
            ImGui::Checkbox("Enable self-learning", &learningEnabled_);
            ImGui::Text("Learned names: %d / rules: %d / pending: %d",
                        correctionMemory_.NameCount(), correctionMemory_.RuleCount(),
                        correctionMemory_.PendingCount());

            bool canConsolidate = !isConsolidating_ && !ctx_->config.apiKey.empty() &&
                                  correctionMemory_.PendingCount() > 0;
            if (!canConsolidate) ImGui::BeginDisabled();
            if (ImGui::Button("Consolidate now")) {
                consolidateFuture_ = correctionMemory_.ConsolidateAsync(ctx_->config.apiKey, "");
                isConsolidating_ = true;
            }
            if (!canConsolidate) ImGui::EndDisabled();
            if (isConsolidating_) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Consolidating...");
            }
            ImGui::TextDisabled("校正のたびに固有名詞・誤り傾向を学習し、語彙と校正例に自動反映します");
        }

        if (isTranscribing_ && transcribeFuture_.valid() &&
            transcribeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            if (transcribeFuture_.get()) {
                detailedResult_ = transcriber_->GetDetailedResult();
                ctx_->transcribedText = detailedResult_.fullText;
                rawTranscript_ = detailedResult_.fullText;
                // 転写直後に自動で LLM 校正を開始（トグル ON 時）
                if (refineEnabled_) {
                    StartRefine(rawTranscript_);
                }
            }
            isTranscribing_ = false;
        }

        // 校正完了を取り込む（成功時のみ transcribedText を校正後に差し替え）
        if (isRefining_ && refineFuture_.valid() &&
            refineFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::string refined = refineFuture_.get();
            if (!refined.empty()) {
                ctx_->transcribedText = refined;

                // 自己進化: 校正ペアを学習し、語彙/ルールを即反映
                if (learningEnabled_) {
                    correctionMemory_.RecordPair(rawTranscript_, refined);
                    ApplyEffectiveVocabulary();

                    // 蓄積がたまったら LLM で一括集約（多重起動は避ける）
                    if (!isConsolidating_ && !ctx_->config.apiKey.empty() &&
                        correctionMemory_.ShouldConsolidate(5)) {
                        consolidateFuture_ = correctionMemory_.ConsolidateAsync(ctx_->config.apiKey, "");
                        isConsolidating_ = true;
                    }
                }
            }
            isRefining_ = false;
        }

        // LLM 集約の完了を取り込む（学習結果を反映）
        if (isConsolidating_ && consolidateFuture_.valid() &&
            consolidateFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            consolidateFuture_.get();
            ApplyEffectiveVocabulary();
            isConsolidating_ = false;
        }

        bool canTranscribe = microphone_->IsCapturing() && !isTranscribing_;
        if (!canTranscribe) ImGui::BeginDisabled();
        if (ImGui::Button("Transcribe")) {
            isTranscribing_ = true;
            transcribeFuture_ = std::async(std::launch::async, [this]() {
                return transcriber_->Transcribe();
            });
        }
        if (!canTranscribe) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Clear Audio")) {
            transcriber_->ClearAudio();
        }

        ImGui::SameLine();
        if (ImGui::Button("Voice Snapshot")) {
            auto snapshot = transcriber_->GetAudioSnapshot();
            if (!snapshot.empty()) {
                ctx_->voiceSnapshotBuffer = std::move(snapshot);
                ctx_->voiceSnapshotSampleRate = 16000;
                ctx_->voiceSnapshotReady = true;
            }
        }

        if (isTranscribing_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Transcribing...");
        }

        ImGui::Spacing();
        ImGui::Checkbox("Auto-refine (LLM校正)", &refineEnabled_);
        ImGui::SameLine();
        if (ctx_->config.apiKey.empty()) {
            ImGui::TextDisabled("(API key 未設定)");
        } else {
            bool canRefine = !rawTranscript_.empty() && !isRefining_;
            if (!canRefine) ImGui::BeginDisabled();
            if (ImGui::Button("Refine Now")) {
                StartRefine(rawTranscript_);
            }
            if (!canRefine) ImGui::EndDisabled();
        }
        if (isRefining_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Refining...");
        }

        ImGui::Spacing();
        ImGui::Text("Result:");
        ImGui::TextWrapped("%s", ctx_->transcribedText.empty() ? "(no result)" : ctx_->transcribedText.c_str());

        // 校正で差し替わっている場合は原文も表示（比較用）
        if (!rawTranscript_.empty() && rawTranscript_ != ctx_->transcribedText) {
            ImGui::TextDisabled("Raw: %s", rawTranscript_.c_str());
        }

        ImGui::Spacing();
        ImGui::Checkbox("Show Details", &showDetailedResult_);
        if (showDetailedResult_ && !detailedResult_.segments.empty()) {
            for (int si = 0; si < static_cast<int>(detailedResult_.segments.size()); ++si) {
                auto& seg = detailedResult_.segments[si];
                float segT0 = seg.t0 * 0.01f;
                float segT1 = seg.t1 * 0.01f;
                ImGui::Separator();
                ImGui::Text("Segment %d [%.2fs - %.2fs]", si, segT0, segT1);
                ImGui::TextWrapped("  %s", seg.text.c_str());

                if (ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(si)), "Tokens (%d)", static_cast<int>(seg.tokens.size()))) {
                    ImGui::Columns(4, "token_cols");
                    ImGui::SetColumnWidth(0, 200);
                    ImGui::SetColumnWidth(1, 80);
                    ImGui::SetColumnWidth(2, 80);
                    ImGui::SetColumnWidth(3, 120);
                    ImGui::Text("Text"); ImGui::NextColumn();
                    ImGui::Text("Prob"); ImGui::NextColumn();
                    ImGui::Text("VLen"); ImGui::NextColumn();
                    ImGui::Text("Time"); ImGui::NextColumn();
                    ImGui::Separator();

                    for (auto& tok : seg.tokens) {
                        ImGui::TextWrapped("%s", tok.text.c_str()); ImGui::NextColumn();
                        ImGui::Text("%.2f%%", tok.probability * 100.0f); ImGui::NextColumn();
                        ImGui::Text("%.3f", tok.voiceLength); ImGui::NextColumn();
                        ImGui::Text("%.2f-%.2f", tok.t0 * 0.01f, tok.t1 * 0.01f); ImGui::NextColumn();
                    }
                    ImGui::Columns(1);
                    ImGui::TreePop();
                }
            }
        }
    }
}
