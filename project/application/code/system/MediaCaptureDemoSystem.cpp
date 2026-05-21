#include "MediaCaptureDemoSystem.h"
#include "SharedMediaContext.h"
#include "AppConfig.h"
#include "MicrophonePanel.h"
#include "WebCameraPanel.h"
#include "ScreenCapturePanel.h"
#include "VoiceVoxPanel.h"
#include "VisionPanel.h"
#include "LLMChatPanel.h"
#include "GatekeeperManager.h"
#include "GatekeeperPanel.h"

#include "imgui/imgui.h"

MediaCaptureDemoSystem::MediaCaptureDemoSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Render) {}

MediaCaptureDemoSystem::~MediaCaptureDemoSystem() = default;

void MediaCaptureDemoSystem::Initialize() {
    ctx_ = std::make_unique<SharedMediaContext>();
    ctx_->config = LoadAppConfig();

    micPanel_ = std::make_unique<MicrophonePanel>();
    camPanel_ = std::make_unique<WebCameraPanel>();
    screenPanel_ = std::make_unique<ScreenCapturePanel>();
    voiceVoxPanel_ = std::make_unique<VoiceVoxPanel>();
    visionPanel_ = std::make_unique<VisionPanel>();
    llmPanel_ = std::make_unique<LLMChatPanel>();
    gkManager_ = std::make_unique<GatekeeperManager>();
    gkPanel_ = std::make_unique<GatekeeperPanel>();

    micPanel_->Initialize(ctx_.get());
    camPanel_->Initialize(ctx_.get());
    screenPanel_->Initialize(ctx_.get());
    voiceVoxPanel_->Initialize(ctx_.get());
    visionPanel_->Initialize(ctx_.get());
    llmPanel_->Initialize(ctx_.get());
    gkManager_->Initialize(ctx_.get());
    gkPanel_->Initialize(ctx_.get(), gkManager_.get());
}

void MediaCaptureDemoSystem::Finalize() {
    gkPanel_->Finalize();
    llmPanel_->Finalize();
    visionPanel_->Finalize();
    voiceVoxPanel_->Finalize();
    screenPanel_->Finalize();
    camPanel_->Finalize();
    micPanel_->Finalize();

    gkPanel_.reset();
    gkManager_.reset();
    llmPanel_.reset();
    visionPanel_.reset();
    voiceVoxPanel_.reset();
    screenPanel_.reset();
    camPanel_.reset();
    micPanel_.reset();
    ctx_.reset();
}

void MediaCaptureDemoSystem::Update() {
    if (ctx_->isSpeaking && ctx_->speakFuture.valid() &&
        ctx_->speakFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        ctx_->speakFuture.get();
        ctx_->isSpeaking = false;
    }

    // ゲートキーパーは UI のタブ選択に関係なく毎フレーム評価する
    gkManager_->Update();

    ImGui::Begin("Media Capture Demo");

    if (ImGui::BeginTabBar("MediaTabs")) {
        if (ImGui::BeginTabItem("Microphone")) {
            micPanel_->Draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("WebCamera")) {
            camPanel_->Draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ScreenCapture")) {
            screenPanel_->Draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VoiceVox")) {
            voiceVoxPanel_->Draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Vision")) {
            visionPanel_->Draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("LLM")) {
            llmPanel_->Draw();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gatekeeper")) {
            gkPanel_->Draw();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
