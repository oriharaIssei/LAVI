#include "MicrophonePanel.h"
#include "SharedMediaContext.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "imgui/imgui.h"
#include "util/StringUtil.h"

#include <cmath>

void MicrophonePanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;

    microphone_ = std::make_unique<OriGine::Microphone>();
    transcriber_ = std::make_unique<WhisperTranscriber>();

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

        if (isTranscribing_ && transcribeFuture_.valid() &&
            transcribeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            if (transcribeFuture_.get()) {
                detailedResult_ = transcriber_->GetDetailedResult();
                ctx_->transcribedText = detailedResult_.fullText;
            }
            isTranscribing_ = false;
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

        if (isTranscribing_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Transcribing...");
        }

        ImGui::Spacing();
        ImGui::Text("Result:");
        ImGui::TextWrapped("%s", ctx_->transcribedText.empty() ? "(no result)" : ctx_->transcribedText.c_str());

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
