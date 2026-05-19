#pragma once

#include "system/ISystem.h"

#include <wrl.h>
#include <d3d12.h>

#include "directX12/DxDescriptor.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace OriGine {
class Microphone;
class WebCamera;
class ScreenCapture;
struct MicrophoneDeviceInfo;
struct WebCameraDeviceInfo;
struct ScreenMonitorInfo;
} // namespace OriGine

struct PreviewTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    OriGine::DxSrvDescriptor srvDescriptor{0};
    uint32_t width  = 0;
    uint32_t height = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Release(OriGine::DxSrvHeap* srvHeap);
};

class MediaCaptureDemoSystem
    : public OriGine::ISystem {
public:
    MediaCaptureDemoSystem();
    ~MediaCaptureDemoSystem() override;

    void Initialize() override;
    void Finalize() override;

protected:
    void Update() override;

private:
    void DrawMicrophonePanel();
    void DrawWebCameraPanel();
    void DrawScreenCapturePanel();

    bool CreatePreviewTexture(PreviewTexture& preview, uint32_t width, uint32_t height);
    void UploadPreviewFrame(PreviewTexture& preview, const uint8_t* data, uint32_t dataSize, uint32_t width, uint32_t height);

    std::unique_ptr<OriGine::Microphone> microphone_;
    std::unique_ptr<OriGine::WebCamera> webCamera_;
    std::unique_ptr<OriGine::ScreenCapture> screenCapture_;

    std::vector<OriGine::MicrophoneDeviceInfo> micDevices_;
    std::vector<OriGine::WebCameraDeviceInfo> camDevices_;
    std::vector<OriGine::ScreenMonitorInfo> monitors_;

    int selectedMicDevice_ = 0;
    int selectedCamDevice_ = 0;
    int selectedMonitor_   = 0;

    std::mutex audioMutex_;
    float currentAudioLevel_ = 0.0f;
    float peakAudioLevel_    = 0.0f;

    std::string recordFilePath_ = "recorded.wav";

    PreviewTexture camPreview_;
    PreviewTexture screenPreview_;
    std::vector<uint8_t> camFrameBuffer_;
    std::vector<uint8_t> screenFrameBuffer_;
};
