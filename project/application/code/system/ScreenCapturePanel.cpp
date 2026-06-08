#include "ScreenCapturePanel.h"
#include "SharedMediaContext.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "Engine.h"
#include "directX12/DxCommand.h"
#include "directX12/DxDevice.h"
#include "imgui/imgui.h"
#include "util/StringUtil.h"

void ScreenCapturePanel::Initialize(SharedMediaContext* ctx) {
    // ScreenCapture の所有・起動は ScreenCaptureSystem。本パネルは ctx から pull するだけ。
    ctx_ = ctx;
}

void ScreenCapturePanel::Finalize() {
    // キャプチャ停止・解放は ScreenCaptureSystem。パネル所有の preview テクスチャのみ解放。
    screenPreview_.Release(OriGine::Engine::GetInstance()->GetSrvHeap());
    screenCapture_ = nullptr;
    ctx_ = nullptr;
}

void ScreenCapturePanel::Draw() {
    // 所有は ScreenCaptureSystem。共有インスタンスを pull する。
    screenCapture_ = ctx_->screenCapture;
    if (!screenCapture_) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(ScreenCaptureSystem 未初期化)");
        return;
    }

    if (monitors_.empty()) {
        monitors_ = OriGine::ScreenCapture::EnumerateMonitors();
    }

    ImGui::Text("Monitors: %d", static_cast<int>(monitors_.size()));
    ImGui::Separator();

    if (!monitors_.empty()) {
        std::string selectedName = ConvertString(monitors_[selectedMonitor_].name);
        if (ImGui::BeginCombo("Monitor", selectedName.c_str())) {
            for (int i = 0; i < static_cast<int>(monitors_.size()); ++i) {
                std::string name = ConvertString(monitors_[i].name);
                char label[256];
                snprintf(label, sizeof(label), "%s (%ux%u)", name.c_str(), monitors_[i].width, monitors_[i].height);
                if (ImGui::Selectable(label, selectedMonitor_ == i)) {
                    selectedMonitor_ = i;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();

    if (!screenCapture_->IsCapturing()) {
        if (ImGui::Button("Open & Start Capture")) {
            OriGine::Engine* engine = OriGine::Engine::GetInstance();
            if (screenCapture_->Open(engine->GetDxDevice(), engine->GetDxCommand(), selectedMonitor_)) {
                screenCapture_->StartCapture();
            }
        }
        if (!screenCapture_->GetLastError().empty()) {
            ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Error: %s", screenCapture_->GetLastError().c_str());
        }
    } else {
        if (ImGui::Button("Stop Capture")) {
            screenCapture_->StopCapture();
            screenCapture_->Close();
            screenPreview_.Release(OriGine::Engine::GetInstance()->GetSrvHeap());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (screenCapture_->IsCapturing()) {
        ImGui::Text("Resolution: %ux%u", screenCapture_->GetWidth(), screenCapture_->GetHeight());

        uint32_t fw = 0, fh = 0;
        if (screenCapture_->GetLatestFrame(ctx_->screenFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
            UploadPreviewFrame(screenPreview_, ctx_->screenFrameBuffer.data(),
                static_cast<uint32_t>(ctx_->screenFrameBuffer.size()), fw, fh);
        }

        if (screenPreview_.texture && screenPreview_.srvDescriptor.GetGpuHandle().ptr != 0) {
            float aspect = static_cast<float>(screenPreview_.width) / static_cast<float>(screenPreview_.height);
            float previewW = ImGui::GetContentRegionAvail().x;
            float previewH = previewW / aspect;
            ImGui::Image(
                reinterpret_cast<ImTextureID>(screenPreview_.srvDescriptor.GetGpuHandle().ptr),
                ImVec2(previewW, previewH));
        }
    }
}
