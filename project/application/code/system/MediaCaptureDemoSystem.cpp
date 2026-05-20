#include "MediaCaptureDemoSystem.h"
#include "WhisperTranscriber.h"
#include "VoiceVoxClient.h"

/// engine
#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "Engine.h"
#include "directX12/DxCommand.h"
#include "directX12/DxDevice.h"
#include "directX12/ResourceStateTracker.h"

///externals
#include "imgui/imgui.h"

/// utils
#include "util/StringUtil.h"

///math
#include <cmath>

namespace {
void TransitionPreviewTexture(
	OriGine::DxCommand* dxCmd,
	PreviewTexture& preview,
	D3D12_RESOURCE_STATES stateAfter){
	if(preview.state == stateAfter){
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource   = preview.texture.Get();
	barrier.Transition.StateBefore = preview.state;
	barrier.Transition.StateAfter  = stateAfter;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	dxCmd->ResourceDirectBarrier(preview.texture,barrier);
	preview.state = stateAfter;
}
} // namespace

void PreviewTexture::Release(OriGine::DxSrvHeap* srvHeap){
	if(srvDescriptor.GetGpuHandle().ptr != 0){
		srvHeap->ReleaseDescriptor(srvDescriptor);
		srvDescriptor = OriGine::DxSrvDescriptor(0);
	}
	if(texture){
		OriGine::ResourceStateTracker::UnregisterResource(texture.Get());
	}
	uploadBuffer.Reset();
	texture.Reset();
	width  = 0;
	height = 0;
	state  = D3D12_RESOURCE_STATE_COMMON;
}

MediaCaptureDemoSystem::MediaCaptureDemoSystem()
	: OriGine::ISystem(OriGine::SystemCategory::Render){}

MediaCaptureDemoSystem::~MediaCaptureDemoSystem() = default;

void MediaCaptureDemoSystem::Initialize(){
	OriGine::WebCamera::StaticInitialize();

	microphone_    = std::make_unique<OriGine::Microphone>();
	webCamera_     = std::make_unique<OriGine::WebCamera>();
	screenCapture_ = std::make_unique<OriGine::ScreenCapture>();
	transcriber_   = std::make_unique<WhisperTranscriber>();
	voiceVox_      = std::make_unique<VoiceVoxClient>();

	micDevices_ = OriGine::Microphone::EnumerateDevices();
	camDevices_ = OriGine::WebCamera::EnumerateDevices();
	monitors_   = OriGine::ScreenCapture::EnumerateMonitors();

	microphone_->SetDataCallback(
		[this](const float* data,uint32_t frameCount,uint32_t channels){
			float rms = 0.0f;
			uint32_t totalSamples = frameCount * channels;
			for(uint32_t i = 0; i < totalSamples; ++i){
				rms += data[i] * data[i];
			}
			rms = std::sqrt(rms / static_cast<float>(totalSamples));

			{
				std::lock_guard<std::mutex> lock(audioMutex_);
				currentAudioLevel_ = rms;
				if(rms > peakAudioLevel_){
					peakAudioLevel_ = rms;
				}
			}

			if(transcriber_ && transcriber_->IsModelLoaded()){
				transcriber_->PushAudio(data,frameCount,channels,microphone_->GetFormat().sampleRate);
			}
		});
}

void MediaCaptureDemoSystem::Finalize(){
	auto* srvHeap = OriGine::Engine::GetInstance()->GetSrvHeap();
	camPreview_.Release(srvHeap);
	screenPreview_.Release(srvHeap);

	if(screenCapture_){
		screenCapture_->StopCapture();
		screenCapture_->Close();
	}
	if(webCamera_){
		webCamera_->StopCapture();
		webCamera_->Close();
	}
	if(microphone_){
		microphone_->StopCapture();
		microphone_->Close();
	}

	if(transcriber_){
		transcriber_->UnloadModel();
	}
	transcriber_.reset();
	if(voiceVox_){
		voiceVox_->Stop();
		voiceVox_->StopEngine();
	}
	voiceVox_.reset();
	screenCapture_.reset();
	webCamera_.reset();
	microphone_.reset();

	OriGine::WebCamera::StaticFinalize();
}

void MediaCaptureDemoSystem::Update(){
	ImGui::Begin("Media Capture Demo");

	if(ImGui::BeginTabBar("MediaTabs")){
		if(ImGui::BeginTabItem("Microphone")){
			DrawMicrophonePanel();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("WebCamera")){
			DrawWebCameraPanel();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("ScreenCapture")){
			DrawScreenCapturePanel();
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("VoiceVox")){
			DrawVoiceVoxPanel();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

bool MediaCaptureDemoSystem::CreatePreviewTexture(PreviewTexture& preview,uint32_t width,uint32_t height){
	auto* engine = OriGine::Engine::GetInstance();
	auto* device = engine->GetDxDevice()->device_.Get();
	auto* srvHeap = engine->GetSrvHeap();

	preview.Release(srvHeap);
	preview.width  = width;
	preview.height = height;

	D3D12_RESOURCE_DESC texDesc{};
	texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width            = width;
	texDesc.Height           = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels        = 1;
	texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES defaultHeap{};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

	HRESULT hr = device->CreateCommittedResource(
		&defaultHeap,D3D12_HEAP_FLAG_NONE,
		&texDesc,D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,IID_PPV_ARGS(&preview.texture));
	if(FAILED(hr)) return false;

	OriGine::ResourceStateTracker::RegisterResource(preview.texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST);
	preview.state = D3D12_RESOURCE_STATE_COPY_DEST;

	UINT64 uploadSize = 0;
	device->GetCopyableFootprints(&texDesc,0,1,0,nullptr,nullptr,nullptr,&uploadSize);

	D3D12_RESOURCE_DESC bufDesc{};
	bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Width            = uploadSize;
	bufDesc.Height           = 1;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.MipLevels        = 1;
	bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	D3D12_HEAP_PROPERTIES uploadHeap{};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

	hr = device->CreateCommittedResource(
		&uploadHeap,D3D12_HEAP_FLAG_NONE,
		&bufDesc,D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,IID_PPV_ARGS(&preview.uploadBuffer));
	if(FAILED(hr)) return false;

	preview.srvDescriptor = srvHeap->AllocateDescriptor();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels    = 1;
	device->CreateShaderResourceView(preview.texture.Get(),&srvDesc,preview.srvDescriptor.GetCpuHandle());

	return true;
}

void MediaCaptureDemoSystem::UploadPreviewFrame(PreviewTexture& preview,const uint8_t* data,uint32_t dataSize,uint32_t width,uint32_t height){
	if(!preview.texture || width != preview.width || height != preview.height){
		if(!CreatePreviewTexture(preview,width,height)) return;
	}

	auto* engine  = OriGine::Engine::GetInstance();
	auto* device  = engine->GetDxDevice()->device_.Get();
	auto* dxCmd   = engine->GetDxCommand();
	auto* cmdList = dxCmd->GetCommandList().Get();

	D3D12_RESOURCE_DESC texDesc = preview.texture->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT numRows = 0;
	UINT64 rowSizeInBytes = 0;
	device->GetCopyableFootprints(&texDesc,0,1,0,&footprint,&numRows,&rowSizeInBytes,nullptr);

	uint8_t* mapped = nullptr;
	D3D12_RANGE readRange{0,0};
	if(FAILED(preview.uploadBuffer->Map(0,&readRange,reinterpret_cast<void**>(&mapped)))) return;

	uint32_t srcRowPitch    = width * 4;
	uint32_t availableRows  = srcRowPitch > 0 ? dataSize / srcRowPitch : 0;
	uint32_t rowsToCopy     = (std::min)(static_cast<uint32_t>(numRows),availableRows);
	for(uint32_t row = 0; row < rowsToCopy; ++row){
		memcpy(mapped + row * footprint.Footprint.RowPitch,data + row * srcRowPitch,srcRowPitch);
	}
	preview.uploadBuffer->Unmap(0,nullptr);

	TransitionPreviewTexture(dxCmd,preview,D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_TEXTURE_COPY_LOCATION dst{};
	dst.pResource        = preview.texture.Get();
	dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION src{};
	src.pResource       = preview.uploadBuffer.Get();
	src.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;

	cmdList->CopyTextureRegion(&dst,0,0,0,&src,nullptr);

	TransitionPreviewTexture(dxCmd,preview,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void MediaCaptureDemoSystem::DrawMicrophonePanel(){
	ImGui::Text("Devices: %d",static_cast<int>(micDevices_.size()));
	ImGui::Separator();

	if(!micDevices_.empty()){
		if(ImGui::BeginCombo("Device",micDevices_[selectedMicDevice_].name.empty()
		   ? "Unknown"
		   : ConvertString(micDevices_[selectedMicDevice_].name).c_str())){
			for(int i = 0; i < static_cast<int>(micDevices_.size()); ++i){
				if(ImGui::Selectable(ConvertString(micDevices_[i].name).c_str(),selectedMicDevice_ == i)){
					selectedMicDevice_ = i;
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Spacing();

	if(!microphone_->IsCapturing()){
		if(ImGui::Button("Open & Start Capture")){
			std::wstring deviceId = micDevices_.empty() ? L"" : micDevices_[selectedMicDevice_].id;
			if(microphone_->Open(deviceId)){
				microphone_->StartCapture();
			}
		}
	} else{
		if(ImGui::Button("Stop Capture")){
			microphone_->StopCapture();
			microphone_->Close();
		}

		ImGui::SameLine();

		if(!microphone_->IsRecording()){
			if(ImGui::Button("Start Recording")){
				microphone_->StartRecording(recordFilePath_);
			}
		} else{
			if(ImGui::Button("Stop Recording")){
				microphone_->StopRecording();
			}
		}
	}

	ImGui::Spacing();
	ImGui::Separator();

	// Audio level visualization
	{
		std::lock_guard<std::mutex> lock(audioMutex_);
		float displayLevel = (std::min)(currentAudioLevel_ * 8.0f,1.0f);
		ImGui::Text("Level:");
		ImGui::SameLine();
		ImGui::ProgressBar(displayLevel,ImVec2(-1,0),"");

		ImGui::Text("RMS: %.6f / Peak: %.6f",currentAudioLevel_,peakAudioLevel_);
		if(ImGui::Button("Reset Peak")){
			peakAudioLevel_ = 0.0f;
		}
	}

	if(microphone_->IsCapturing()){
		auto& fmt = microphone_->GetFormat();
		ImGui::Text("Format: %uHz / %uch / %ubit",fmt.sampleRate,fmt.channels,fmt.bitsPerSample);
		auto stats = microphone_->GetStats();
		ImGui::Text("Packets: %llu / Frames: %llu / Flags: 0x%08X / LastError: 0x%08X",
			static_cast<unsigned long long>(stats.packetCount),
			static_cast<unsigned long long>(stats.frameCount),
			stats.lastFlags,
			static_cast<uint32_t>(stats.lastError));
	}

	if(microphone_->IsRecording()){
		ImGui::TextColored(ImVec4(1,0,0,1),"Recording: %s",recordFilePath_.c_str());
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Speech Recognition (Whisper)");
	ImGui::Separator();

	if(!transcriber_->IsModelLoaded()){
		ImGui::InputText("Model Path",whisperModelPath_.data(),whisperModelPath_.capacity() + 1,ImGuiInputTextFlags_CallbackResize,
			[](ImGuiInputTextCallbackData* data) -> int{
				if(data->EventFlag == ImGuiInputTextFlags_CallbackResize){
					auto* str = static_cast<std::string*>(data->UserData);
					str->resize(data->BufTextLen);
					data->Buf = str->data();
				}
				return 0;
			},&whisperModelPath_);
		if(ImGui::Button("Load Model")){
			if(transcriber_->LoadModel(whisperModelPath_)){
				transcriber_->SetVadModelPath(vadModelPath_);
			}
		}
	} else{
		ImGui::Text("Model: Loaded");
		ImGui::SameLine();
		if(ImGui::Button("Unload Model")){
			transcriber_->UnloadModel();
			transcriber_->ClearAudio();
			transcribedText_.clear();
		}

		size_t bufferSamples = transcriber_->GetAudioSampleCount();
		float bufferSeconds = bufferSamples / 16000.0f;
		ImGui::Text("Audio Buffer: %zu samples (%.1f sec)", bufferSamples, bufferSeconds);

		if(isTranscribing_ && transcribeFuture_.valid() &&
			transcribeFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready){
			if(transcribeFuture_.get()){
				detailedResult_ = transcriber_->GetDetailedResult();
				transcribedText_ = detailedResult_.fullText;
			}
			isTranscribing_ = false;
		}

		bool canTranscribe = microphone_->IsCapturing() && !isTranscribing_;
		if(!canTranscribe){ ImGui::BeginDisabled(); }
		if(ImGui::Button("Transcribe")){
			isTranscribing_ = true;
			transcribeFuture_ = std::async(std::launch::async,[this](){
				return transcriber_->Transcribe();
			});
		}
		if(!canTranscribe){ ImGui::EndDisabled(); }

		ImGui::SameLine();
		if(ImGui::Button("Clear Audio")){
			transcriber_->ClearAudio();
		}

		if(isTranscribing_){
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1,1,0,1),"Transcribing...");
		}

		ImGui::Spacing();
		ImGui::Text("Result:");
		ImGui::TextWrapped("%s",transcribedText_.empty() ? "(no result)" : transcribedText_.c_str());

		ImGui::Spacing();
		ImGui::Checkbox("Show Details",&showDetailedResult_);
		if(showDetailedResult_ && !detailedResult_.segments.empty()){
			for(int si = 0; si < static_cast<int>(detailedResult_.segments.size()); ++si){
				auto& seg = detailedResult_.segments[si];
				float segT0 = seg.t0 * 0.01f;
				float segT1 = seg.t1 * 0.01f;
				ImGui::Separator();
				ImGui::Text("Segment %d [%.2fs - %.2fs]",si,segT0,segT1);
				ImGui::TextWrapped("  %s",seg.text.c_str());

				if(ImGui::TreeNode(reinterpret_cast<void*>(static_cast<intptr_t>(si)),"Tokens (%d)",static_cast<int>(seg.tokens.size()))){
					ImGui::Columns(4,"token_cols");
					ImGui::SetColumnWidth(0,200);
					ImGui::SetColumnWidth(1,80);
					ImGui::SetColumnWidth(2,80);
					ImGui::SetColumnWidth(3,120);
					ImGui::Text("Text"); ImGui::NextColumn();
					ImGui::Text("Prob"); ImGui::NextColumn();
					ImGui::Text("VLen"); ImGui::NextColumn();
					ImGui::Text("Time"); ImGui::NextColumn();
					ImGui::Separator();

					for(auto& tok : seg.tokens){
						ImGui::TextWrapped("%s",tok.text.c_str()); ImGui::NextColumn();
						ImGui::Text("%.2f%%",tok.probability * 100.0f); ImGui::NextColumn();
						ImGui::Text("%.3f",tok.voiceLength); ImGui::NextColumn();
						ImGui::Text("%.2f-%.2f",tok.t0 * 0.01f,tok.t1 * 0.01f); ImGui::NextColumn();
					}
					ImGui::Columns(1);
					ImGui::TreePop();
				}
			}
		}
	}
}

void MediaCaptureDemoSystem::DrawWebCameraPanel(){
	ImGui::Text("Devices: %d",static_cast<int>(camDevices_.size()));
	ImGui::Separator();

	if(!camDevices_.empty()){
		std::string selectedName = ConvertString(camDevices_[selectedCamDevice_].name);
		if(ImGui::BeginCombo("Device",selectedName.c_str())){
			for(int i = 0; i < static_cast<int>(camDevices_.size()); ++i){
				std::string name = ConvertString(camDevices_[i].name);
				if(ImGui::Selectable(name.c_str(),selectedCamDevice_ == i)){
					selectedCamDevice_ = i;
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Spacing();

	if(!webCamera_->IsCapturing()){
		if(ImGui::Button("Open & Start Capture")){
			std::wstring deviceId = camDevices_.empty() ? L"" : camDevices_[selectedCamDevice_].id;
			if(webCamera_->Open(deviceId,640,480)){
				webCamera_->StartCapture();
			}
		}
	} else{
		if(ImGui::Button("Stop Capture")){
			webCamera_->StopCapture();
			webCamera_->Close();
			camPreview_.Release(OriGine::Engine::GetInstance()->GetSrvHeap());
		}
	}

	ImGui::Spacing();
	ImGui::Separator();

	if(webCamera_->IsCapturing()){
		ImGui::Text("Resolution: %ux%u",webCamera_->GetWidth(),webCamera_->GetHeight());

		uint32_t fw = 0,fh = 0;
		if(webCamera_->GetLatestFrame(camFrameBuffer_,fw,fh) && fw > 0 && fh > 0){
			UploadPreviewFrame(camPreview_,camFrameBuffer_.data(),static_cast<uint32_t>(camFrameBuffer_.size()),fw,fh);
		}

		if(camPreview_.texture && camPreview_.srvDescriptor.GetGpuHandle().ptr != 0){
			float aspect     = static_cast<float>(camPreview_.width) / static_cast<float>(camPreview_.height);
			float previewW   = ImGui::GetContentRegionAvail().x;
			float previewH   = previewW / aspect;
			ImGui::Image(
				reinterpret_cast<ImTextureID>(camPreview_.srvDescriptor.GetGpuHandle().ptr),
				ImVec2(previewW,previewH));
		}
	}
}

void MediaCaptureDemoSystem::DrawScreenCapturePanel(){
	ImGui::Text("Monitors: %d",static_cast<int>(monitors_.size()));
	ImGui::Separator();

	if(!monitors_.empty()){
		std::string selectedName = ConvertString(monitors_[selectedMonitor_].name);
		if(ImGui::BeginCombo("Monitor",selectedName.c_str())){
			for(int i = 0; i < static_cast<int>(monitors_.size()); ++i){
				std::string name = ConvertString(monitors_[i].name);
				char label[256];
				snprintf(label,sizeof(label),"%s (%ux%u)",name.c_str(),monitors_[i].width,monitors_[i].height);
				if(ImGui::Selectable(label,selectedMonitor_ == i)){
					selectedMonitor_ = i;
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Spacing();

	if(!screenCapture_->IsCapturing()){
		if(ImGui::Button("Open & Start Capture")){
			OriGine::Engine* engine = OriGine::Engine::GetInstance();
			if(screenCapture_->Open(engine->GetDxDevice(),engine->GetDxCommand(),selectedMonitor_)){
				screenCapture_->StartCapture();
			}
		}
	} else{
		if(ImGui::Button("Stop Capture")){
			screenCapture_->StopCapture();
			screenCapture_->Close();
			screenPreview_.Release(OriGine::Engine::GetInstance()->GetSrvHeap());
		}
	}

	ImGui::Spacing();
	ImGui::Separator();

	if(screenCapture_->IsCapturing()){
		ImGui::Text("Resolution: %ux%u",screenCapture_->GetWidth(),screenCapture_->GetHeight());

		uint32_t fw = 0,fh = 0;
		if(screenCapture_->GetLatestFrame(screenFrameBuffer_,fw,fh) && fw > 0 && fh > 0){
			UploadPreviewFrame(screenPreview_,screenFrameBuffer_.data(),static_cast<uint32_t>(screenFrameBuffer_.size()),fw,fh);
		}

		if(screenPreview_.texture && screenPreview_.srvDescriptor.GetGpuHandle().ptr != 0){
			float aspect     = static_cast<float>(screenPreview_.width) / static_cast<float>(screenPreview_.height);
			float previewW   = ImGui::GetContentRegionAvail().x;
			float previewH   = previewW / aspect;
			ImGui::Image(
				reinterpret_cast<ImTextureID>(screenPreview_.srvDescriptor.GetGpuHandle().ptr),
				ImVec2(previewW,previewH));
		}
	}
}

void MediaCaptureDemoSystem::DrawVoiceVoxPanel(){
	ImGui::Text("VoiceVox (Text-to-Speech)");
	ImGui::Separator();

	// Engine path & start/stop
	ImGui::InputText("Engine Path",voiceVoxEnginePath_.data(),voiceVoxEnginePath_.capacity() + 1,
		ImGuiInputTextFlags_CallbackResize,
		[](ImGuiInputTextCallbackData* data) -> int{
			if(data->EventFlag == ImGuiInputTextFlags_CallbackResize){
				auto* str = static_cast<std::string*>(data->UserData);
				str->resize(data->BufTextLen);
				data->Buf = str->data();
			}
			return 0;
		},&voiceVoxEnginePath_);

	bool engineRunning = voiceVox_->IsEngineRunning();
	bool engineReady = engineRunning && voiceVox_->IsEngineReady();

	if(!engineRunning){
		if(ImGui::Button("Start Engine")){
			voiceVox_->StartEngine(voiceVoxEnginePath_);
			if(voiceVox_->IsEngineReady()){
				voiceVoxSpeakers_ = voiceVox_->GetSpeakers();
			}
		}
	} else{
		if(ImGui::Button("Stop Engine")){
			voiceVox_->StopEngine();
			voiceVoxSpeakers_.clear();
		}
		ImGui::SameLine();
		if(engineReady){
			ImGui::TextColored(ImVec4(0,1,0,1),"Ready");
		} else{
			ImGui::TextColored(ImVec4(1,1,0,1),"Starting...");
		}
	}

	if(!engineReady){
		return;
	}

	ImGui::Spacing();
	ImGui::Separator();

	// Speaker selection
	if(voiceVoxSpeakers_.empty()){
		if(ImGui::Button("Refresh Speakers")){
			voiceVoxSpeakers_ = voiceVox_->GetSpeakers();
		}
	} else{
		std::string selectedLabel = voiceVoxSpeakers_[selectedSpeaker_].name
			+ " (" + voiceVoxSpeakers_[selectedSpeaker_].styleName + ")";
		if(ImGui::BeginCombo("Speaker",selectedLabel.c_str())){
			for(int i = 0; i < static_cast<int>(voiceVoxSpeakers_.size()); ++i){
				std::string label = voiceVoxSpeakers_[i].name
					+ " (" + voiceVoxSpeakers_[i].styleName + ")";
				if(ImGui::Selectable(label.c_str(),selectedSpeaker_ == i)){
					selectedSpeaker_ = i;
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Spacing();

	// Text input
	ImGui::InputTextMultiline("##voicevox_text",voiceVoxText_.data(),voiceVoxText_.capacity() + 1,
		ImVec2(-1,80),
		ImGuiInputTextFlags_CallbackResize,
		[](ImGuiInputTextCallbackData* data) -> int{
			if(data->EventFlag == ImGuiInputTextFlags_CallbackResize){
				auto* str = static_cast<std::string*>(data->UserData);
				str->resize(data->BufTextLen);
				data->Buf = str->data();
			}
			return 0;
		},&voiceVoxText_);

	ImGui::Spacing();

	// Check async result
	if(isSpeaking_ && speakFuture_.valid() &&
		speakFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready){
		speakFuture_.get();
		isSpeaking_ = false;
	}

	// Speak button
	bool canSpeak = !voiceVoxText_.empty() && !voiceVoxSpeakers_.empty() && !isSpeaking_;
	if(!canSpeak){ ImGui::BeginDisabled(); }
	if(ImGui::Button("Speak")){
		isSpeaking_ = true;
		int speakerId = voiceVoxSpeakers_[selectedSpeaker_].id;
		speakFuture_ = voiceVox_->SpeakAsync(voiceVoxText_,speakerId);
	}
	if(!canSpeak){ ImGui::EndDisabled(); }

	if(isSpeaking_){
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1,1,0,1),"Speaking...");
	}

	ImGui::SameLine();
	if(ImGui::Button("Stop")){
		voiceVox_->Stop();
	}

	// Whisper -> VoiceVox pipeline
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Whisper -> VoiceVox");
	if(!transcribedText_.empty()){
		if(ImGui::Button("Speak Transcription")){
			if(!isSpeaking_ && !voiceVoxSpeakers_.empty()){
				isSpeaking_ = true;
				int speakerId = voiceVoxSpeakers_[selectedSpeaker_].id;
				speakFuture_ = voiceVox_->SpeakAsync(transcribedText_,speakerId);
			}
		}
		ImGui::SameLine();
		ImGui::TextWrapped("%s",transcribedText_.c_str());
	} else{
		ImGui::TextDisabled("(Transcribe audio first in Microphone tab)");
	}
}
