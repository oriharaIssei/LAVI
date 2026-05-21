#include "ScreenGatekeeperPanel.h"
#include "SharedMediaContext.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "imgui/imgui.h"

#include <cstdio>

void ScreenGatekeeperPanel::Initialize(SharedMediaContext* ctx) {
    ctx_ = ctx;
    gk_ = std::make_unique<ScreenGatekeeper>();
    gk_->SetWatchForeground(watchForeground_);
    gk_->SetDetectNewProcesses(detectNew_);
    gk_->SetScreenDiffThreshold(screenDiffThreshold_);
    gk_->SetPixelDiffThreshold(pixelDiffThreshold_);
}

void ScreenGatekeeperPanel::Finalize() {
    gk_.reset();
}

void ScreenGatekeeperPanel::Evaluate() {
    const uint8_t* data = nullptr;
    uint32_t fw = 0, fh = 0;
    if (useScreenDiff_ && ctx_->screenCapture && ctx_->screenCapture->IsCapturing()) {
        if (ctx_->screenCapture->GetLatestFrame(ctx_->screenFrameBuffer, fw, fh) && fw > 0 && fh > 0) {
            data = ctx_->screenFrameBuffer.data();
        } else {
            fw = fh = 0;
        }
    }

    lastResult_ = gk_->Evaluate(data, fw, fh);

    if (lastResult_.triggered) {
        const double now = ImGui::GetTime();
        if (now - lastTriggerTime_ >= cooldownSec_) {
            lastTriggerTime_ = now;
            std::string msg = "[trigger]";
            if (lastResult_.foregroundChanged) msg += " fg->" + lastResult_.foregroundApp;
            if (!lastResult_.newApps.empty()) {
                msg += " new:";
                for (size_t i = 0; i < lastResult_.newApps.size(); ++i) {
                    if (i) msg += ",";
                    msg += lastResult_.newApps[i];
                }
            }
            if (lastResult_.screenChanged) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), " screen %.1f%%", lastResult_.screenDiffRatio * 100.0f);
                msg += buf;
            }
            triggerLog_.push_back(msg);
            if (triggerLog_.size() > 20) triggerLog_.erase(triggerLog_.begin());
        }
    }
}

void ScreenGatekeeperPanel::Draw() {
    ImGui::Text("Screen Gatekeeper (WinAPI process + OpenCV diff)");
    ImGui::Separator();

    if (ImGui::Checkbox("Watch foreground switch", &watchForeground_)) {
        gk_->SetWatchForeground(watchForeground_);
    }
    if (ImGui::Checkbox("Detect new windows", &detectNew_)) {
        gk_->SetDetectNewProcesses(detectNew_);
    }
    ImGui::Checkbox("Use screen diff (needs ScreenCapture)", &useScreenDiff_);

    if (ImGui::CollapsingHeader("Screen Diff", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SliderFloat("change ratio threshold", &screenDiffThreshold_, 0.0f, 0.5f)) {
            gk_->SetScreenDiffThreshold(screenDiffThreshold_);
        }
        if (ImGui::SliderInt("pixel diff threshold", &pixelDiffThreshold_, 0, 128)) {
            gk_->SetPixelDiffThreshold(pixelDiffThreshold_);
        }
    }

    if (ImGui::CollapsingHeader("Monitoring", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("interval (sec)", &intervalSec_, 0.1f, 3.0f);
        ImGui::SliderFloat("cooldown (sec)", &cooldownSec_, 0.0f, 5.0f);
    }

    ImGui::Spacing();
    ImGui::Checkbox("Auto Monitor", &autoMonitor_);
    ImGui::SameLine();
    if (ImGui::Button("Evaluate Once")) {
        Evaluate();
    }

    if (autoMonitor_) {
        const double now = ImGui::GetTime();
        if (now - lastEvalTime_ >= intervalSec_) {
            lastEvalTime_ = now;
            Evaluate();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Foreground: %s",
                lastResult_.foregroundApp.empty() ? "(none)" : lastResult_.foregroundApp.c_str());
    ImGui::TextWrapped("Title: %s", lastResult_.foregroundTitle.c_str());
    ImGui::Text("Screen diff: %.1f%%", lastResult_.screenDiffRatio * 100.0f);
    ImGui::ProgressBar(lastResult_.screenDiffRatio, ImVec2(-1, 0));

    ImGui::Spacing();
    ImGui::Text("Triggers:");
    ImGui::BeginChild("screen_triggers", ImVec2(0, 120), true);
    for (const auto& t : triggerLog_) {
        ImGui::TextUnformatted(t.c_str());
    }
    ImGui::EndChild();
}
