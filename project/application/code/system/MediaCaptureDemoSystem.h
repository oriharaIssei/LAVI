#pragma once

#include "system/ISystem.h"

#include <wrl.h>
#include <d3d12.h>

#include "directX12/DxDescriptor.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class WhisperTranscriber;
#include "WhisperTranscriber.h"
#include "VoiceVoxClient.h"
#include "VisionAnalyzer.h"

namespace OriGine
{
    class Microphone;
    class WebCamera;
    class ScreenCapture;
    struct MicrophoneDeviceInfo;
    struct WebCameraDeviceInfo;
    struct ScreenMonitorInfo;
} // namespace OriGine

struct PreviewTexture
{
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    OriGine::DxSrvDescriptor srvDescriptor{0};
    uint32_t width = 0;
    uint32_t height = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;

    void Release(OriGine::DxSrvHeap *srvHeap);
};

class MediaCaptureDemoSystem
    : public OriGine::ISystem
{
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
    void DrawVoiceVoxPanel();
    void DrawVisionPanel();

    void LoadApiConfig();
    void SaveApiConfig();

    bool CreatePreviewTexture(PreviewTexture &preview, uint32_t width, uint32_t height);
    void UploadPreviewFrame(PreviewTexture &preview, const uint8_t *data, uint32_t dataSize, uint32_t width, uint32_t height);

    std::unique_ptr<OriGine::Microphone> microphone_;
    std::unique_ptr<OriGine::WebCamera> webCamera_;
    std::unique_ptr<OriGine::ScreenCapture> screenCapture_;

    std::vector<OriGine::MicrophoneDeviceInfo> micDevices_;
    std::vector<OriGine::WebCameraDeviceInfo> camDevices_;
    std::vector<OriGine::ScreenMonitorInfo> monitors_;

    int selectedMicDevice_ = 0;
    int selectedCamDevice_ = 0;
    int selectedMonitor_ = 0;

    std::mutex audioMutex_;
    float currentAudioLevel_ = 0.0f;
    float peakAudioLevel_ = 0.0f;

    std::string recordFilePath_ = "recorded.wav";

    PreviewTexture camPreview_;
    PreviewTexture screenPreview_;
    std::vector<uint8_t> camFrameBuffer_;
    std::vector<uint8_t> screenFrameBuffer_;

    std::unique_ptr<WhisperTranscriber> transcriber_;
    std::string whisperModelPath_ = "application/resource/whisper/ggml-large-v3.bin";
    std::string vadModelPath_ = "application/resource/whisper/ggml-silero-v6.2.0.bin";
    std::string transcribedText_;
    WhisperResult detailedResult_;
    bool showDetailedResult_ = false;
    std::future<bool> transcribeFuture_;
    bool isTranscribing_ = false;

    // VoiceVox
    std::unique_ptr<VoiceVoxClient> voiceVox_;
    std::string voiceVoxEnginePath_ = "application/resource/voiceVox/windows-nvidia/run.exe";
    std::string voiceVoxText_;
    std::vector<VoiceVoxSpeaker> voiceVoxSpeakers_;
    int selectedSpeaker_ = 0;
    std::future<bool> speakFuture_;
    bool isSpeaking_ = false;

    // Vision
    std::unique_ptr<VisionAnalyzer> visionAnalyzer_;
    std::string visionApiKey_;
    std::string visionPrompt_ = "この画像に写っているものを日本語で説明してください。";
    std::string visionResult_;
    std::future<VisionResult> visionFuture_;
    bool isVisionAnalyzing_ = false;
    int visionSource_ = 0; // 0=WebCamera, 1=ScreenCapture
};
