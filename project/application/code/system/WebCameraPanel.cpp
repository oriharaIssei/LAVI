#include "WebCameraPanel.h"
#include "SharedMediaContext.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "Engine.h"
#include "imgui/imgui.h"
#include "util/StringUtil.h"

void WebCameraPanel::Initialize(SharedMediaContext* ctx) {
    // WebCamera の所有・StaticInitialize/起動は WebCameraSystem。本パネルは ctx から pull するだけ。
    ctx_ = ctx;
}

void WebCameraPanel::Finalize() {
    // キャプチャ停止・StaticFinalize は WebCameraSystem。パネル所有の preview テクスチャのみ解放。
    camPreview_.Release(OriGine::Engine::GetInstance()->GetSrvHeap());
    webCamera_ = nullptr;
    ctx_ = nullptr;
}

void WebCameraPanel::Draw() {
    // 所有は WebCameraSystem。共有インスタンスを pull する。
    webCamera_ = ctx_->webCamera;
    if (!webCamera_) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(WebCameraSystem 未初期化)");
        return;
    }

    // デバイス一覧は初回のみ列挙（StaticInitialize は System が実施済み）。
    if (camDevices_.empty()) {
        camDevices_ = OriGine::WebCamera::EnumerateDevices();
    }

    ImGui::Text("Devices: %d", static_cast<int>(camDevices_.size()));
    ImGui::Separator();

    if (!camDevices_.empty()) {
        std::string selectedName = ConvertString(camDevices_[selectedCamDevice_].name);
        if (ImGui::BeginCombo("Device", selectedName.c_str())) {
            for (int i = 0; i < static_cast<int>(camDevices_.size()); ++i) {
                std::string name = ConvertString(camDevices_[i].name);
                if (ImGui::Selectable(name.c_str(), selectedCamDevice_ == i)) {
                    selectedCamDevice_ = i;
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();

    if (!webCamera_->IsCapturing()) {
        if (ImGui::Button("Open & Start Capture")) {
            std::wstring deviceId = camDevices_.empty() ? L"" : camDevices_[selectedCamDevice_].id;
            if (webCamera_->Open(deviceId, 640, 480)) {
                webCamera_->StartCapture();
            }
        }
    } else {
        if (ImGui::Button("Stop Capture")) {
            webCamera_->StopCapture();
            webCamera_->Close();
            camPreview_.Release(OriGine::Engine::GetInstance()->GetSrvHeap());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (webCamera_->IsCapturing()) {
        ImGui::Text("Resolution: %ux%u", webCamera_->GetWidth(), webCamera_->GetHeight());

        uint32_t fw = 0, fh = 0;
        if (webCamera_->GetLatestFrame(ctx_->camFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
            UploadPreviewFrame(camPreview_, ctx_->camFrameBuffer.data(),
                static_cast<uint32_t>(ctx_->camFrameBuffer.size()), fw, fh);
        }

        if (camPreview_.texture && camPreview_.srvDescriptor.GetGpuHandle().ptr != 0) {
            float aspect = static_cast<float>(camPreview_.width) / static_cast<float>(camPreview_.height);
            float previewW = ImGui::GetContentRegionAvail().x;
            float previewH = previewW / aspect;
            ImGui::Image(
                reinterpret_cast<ImTextureID>(camPreview_.srvDescriptor.GetGpuHandle().ptr),
                ImVec2(previewW, previewH));
        }
    }
}
