#include "WebCameraPanel.h"
#include "SharedMediaContext.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "Engine.h"
#include "imgui/imgui.h"
#include "util/StringUtil.h"

void WebCameraPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;

    OriGine::WebCamera::StaticInitialize();

    webCamera_ = std::make_unique<OriGine::WebCamera>();
    camDevices_ = OriGine::WebCamera::EnumerateDevices();

    ctx_->webCamera = webCamera_.get();
}

void WebCameraPanel::Finalize() {
    if (webCamera_) {
        webCamera_->StopCapture();
        webCamera_->Close();
    }
    camPreview_.Release(OriGine::Engine::GetInstance()->GetSrvHeap());
    webCamera_.reset();

    OriGine::WebCamera::StaticFinalize();
}

void WebCameraPanel::Draw() {
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
