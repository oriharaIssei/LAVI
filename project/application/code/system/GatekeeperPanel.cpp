#include "GatekeeperPanel.h"
#include "SharedMediaContext.h"
#include "GatekeeperManager.h"

#define ENGINE_INCLUDE
#define ENGINE_MEDIA_CAPTURE
#include <EngineInclude.h>

#include "imgui/imgui.h"
#include "util/StringUtil.h"

#include <cstring>
#include <vector>

namespace {
int InputTextResize(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(data->BufTextLen);
        data->Buf = str->data();
    }
    return 0;
}
}  // namespace

void GatekeeperPanel::Initialize(SharedMediaContext* ctx, GatekeeperManager* mgr) {
    ctx_ = ctx;
    mgr_ = mgr;

    // 既定パラメータを各 GK に反映
    PushCameraParams();
    PushScreenParams();
    ApplyKeywords();

    // ウェイクワードバッファ初期化
    std::strncpy(wakeWordBuf_, ctx_->wakeWord.c_str(), sizeof(wakeWordBuf_) - 1);
    hotkeyKeyBuf_[0] = static_cast<char>(ctx_->hotkeyVk);
    hotkeyKeyBuf_[1] = '\0';
    if (ctx_->hotkeyModifiers == (0x0002 | 0x0004)) hotkeyModSel_ = 0;      // Ctrl+Shift
    else if (ctx_->hotkeyModifiers == (0x0002 | 0x0001)) hotkeyModSel_ = 1;  // Ctrl+Alt
    else if (ctx_->hotkeyModifiers == (0x0001 | 0x0004)) hotkeyModSel_ = 2;  // Alt+Shift
}

void GatekeeperPanel::Finalize() {
    ctx_ = nullptr;
    mgr_ = nullptr;
}

void GatekeeperPanel::PushCameraParams() {
    auto* cam = mgr_->Camera();
    cam->SetConfidenceThreshold(camThreshold_);
    cam->SetDetectionParams(scaleFactor_, minNeighbors_, minFaceSize_);
    cam->SetConfirmFrames(confirmFrames_);
    cam->SetIgnoreNeutral(ignoreNeutral_);
    cam->SetNeutralBias(neutralBias_);
}

void GatekeeperPanel::PushScreenParams() {
    auto* scr = mgr_->Screen();
    scr->SetWatchForeground(watchFg_);
    scr->SetDetectNewProcesses(detectNew_);
    scr->SetScreenDiffThreshold(screenDiffThreshold_);
    scr->SetPixelDiffThreshold(pixelDiffThreshold_);
}

void GatekeeperPanel::ApplyKeywords() {
    std::vector<std::string> kws;
    std::string line;
    for (char c : keywordText_) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) kws.push_back(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) kws.push_back(line);
    mgr_->Mic()->SetKeywords(kws);
    mgr_->Mic()->SetCaseSensitive(caseSensitive_);
}

void GatekeeperPanel::Draw() {
    auto& cfg = mgr_->config();

    ImGui::Text("Gatekeeper (integrated)");
    ImGui::Separator();

    // ===== Integration =====
    if (ImGui::CollapsingHeader("Integration", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Camera##en", &cfg.camEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Screen##en", &cfg.screenEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Mic##en", &cfg.micEnabled);
        if (!camLoaded_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(load camera model below)");
        }
        ImGui::SliderFloat("combine window (sec)", &cfg.combineWindow, 0.2f, 5.0f);

        ImGui::Separator();
        ImGui::Checkbox("Auto escalate (Claude)", &cfg.autoEscalate);
        ImGui::SameLine();
        ImGui::Checkbox("Auto speak (VoiceVox)", &cfg.autoSpeak);
        ImGui::SliderFloat("escalate cooldown (sec)", &cfg.escalateCooldown, 2.0f, 30.0f);

        if (ImGui::Button("Escalate latest now")) mgr_->EscalateLatest();
        ImGui::SameLine();
        ImGui::TextColored(mgr_->LlmBusy() ? ImVec4(1, 1, 0, 1) : ImVec4(0.6f, 0.6f, 0.6f, 1),
                           "%s", mgr_->LlmBusy() ? "Claude: thinking..." : "Claude: idle");
        ImGui::SameLine();
        if (ImGui::Button("Clear Log")) mgr_->ClearLog();

        if (ctx_->config.apiKey.empty()) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Set API key (LLM/Vision tab) to escalate");
        }
        if (cfg.autoSpeak && (!ctx_->voiceVox || !ctx_->voiceVox->IsEngineReady())) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Start VoiceVox engine to speak");
        }
        if (!mgr_->LastResponse().empty()) {
            ImGui::TextUnformatted("Last response:");
            ImGui::TextWrapped("%s", mgr_->LastResponse().c_str());
        }
    }

    // ===== Camera GK =====
    if (ImGui::CollapsingHeader("Camera GK (FER+)")) {
        ImGui::InputText("FER+ Model", ferModelPath_.data(), ferModelPath_.capacity() + 1,
                         ImGuiInputTextFlags_CallbackResize, InputTextResize, &ferModelPath_);
        ImGui::InputText("Haar XML", haarPath_.data(), haarPath_.capacity() + 1,
                         ImGuiInputTextFlags_CallbackResize, InputTextResize, &haarPath_);
        ImGui::Checkbox("Use GPU", &camUseGpu_);
        if (ImGui::Button(camLoaded_ ? "Reload Models" : "Load Models")) {
            camLoaded_ = mgr_->Camera()->Initialize(ConvertString(ferModelPath_), haarPath_, camUseGpu_);
            camStatus_ = camLoaded_ ? "Loaded." : ("[Error] " + mgr_->Camera()->LastError());
            if (camLoaded_) {
                PushCameraParams();
                mgr_->Camera()->ResetState();
                cfg.camEnabled = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextColored(camLoaded_ ? ImVec4(0, 1, 0, 1) : ImVec4(1, 0.5f, 0, 1),
                           "%s", camStatus_.empty() ? "(not loaded)" : camStatus_.c_str());

        if (ImGui::SliderFloat("confidence", &camThreshold_, 0.0f, 1.0f))
            mgr_->Camera()->SetConfidenceThreshold(camThreshold_);
        bool dc = false;
        dc |= ImGui::SliderFloat("scaleFactor", &scaleFactor_, 1.05f, 1.4f);
        dc |= ImGui::SliderInt("minNeighbors", &minNeighbors_, 1, 10);
        dc |= ImGui::SliderInt("minFaceSize", &minFaceSize_, 0, 300);
        if (dc) mgr_->Camera()->SetDetectionParams(scaleFactor_, minNeighbors_, minFaceSize_);
        if (ImGui::SliderInt("confirm frames", &confirmFrames_, 1, 15))
            mgr_->Camera()->SetConfirmFrames(confirmFrames_);
        if (ImGui::Checkbox("ignore neutral", &ignoreNeutral_))
            mgr_->Camera()->SetIgnoreNeutral(ignoreNeutral_);
        if (ImGui::SliderFloat("neutral suppression", &neutralBias_, 0.0f, 0.95f))
            mgr_->Camera()->SetNeutralBias(neutralBias_);

        const EmotionResult& cr = mgr_->CameraResult();
        ImGui::Text("Face: %s  Dominant: %s (%.0f%%)",
                    cr.faceDetected ? "yes" : "no",
                    CameraGatekeeper::EmotionName(cr.dominant), cr.confidence * 100.0f);
    }

    // ===== Screen GK =====
    if (ImGui::CollapsingHeader("Screen GK")) {
        if (ImGui::Checkbox("Watch foreground", &watchFg_)) mgr_->Screen()->SetWatchForeground(watchFg_);
        if (ImGui::Checkbox("Detect new windows", &detectNew_)) mgr_->Screen()->SetDetectNewProcesses(detectNew_);
        ImGui::Checkbox("Use screen diff", &useScreenDiff_);
        if (ImGui::SliderFloat("change ratio", &screenDiffThreshold_, 0.0f, 0.5f))
            mgr_->Screen()->SetScreenDiffThreshold(screenDiffThreshold_);
        if (ImGui::SliderInt("pixel diff", &pixelDiffThreshold_, 0, 128))
            mgr_->Screen()->SetPixelDiffThreshold(pixelDiffThreshold_);

        const ScreenGateResult& sr = mgr_->ScreenResult();
        ImGui::Text("Foreground: %s", sr.foregroundApp.empty() ? "(none)" : sr.foregroundApp.c_str());
        ImGui::Text("Screen diff: %.1f%%", sr.screenDiffRatio * 100.0f);
    }

    // ===== Mic GK =====
    if (ImGui::CollapsingHeader("Mic GK")) {
        ImGui::TextUnformatted("Keywords (1 per line):");
        if (ImGui::InputTextMultiline("##kw", keywordText_.data(), keywordText_.capacity() + 1,
                                      ImVec2(-1, 70), ImGuiInputTextFlags_CallbackResize,
                                      InputTextResize, &keywordText_)) {
            ApplyKeywords();
        }
        if (ImGui::Checkbox("Case sensitive (ASCII)", &caseSensitive_)) ApplyKeywords();
        ImGui::TextWrapped("Transcript: %s",
                           ctx_->transcribedText.empty() ? "(none)" : ctx_->transcribedText.c_str());
    }

    // ===== Hotkey / Wake Word =====
    if (ImGui::CollapsingHeader("Hotkey / Wake Word")) {
        ImGui::Checkbox("Hotkey enabled", &ctx_->hotkeyEnabled);
        if (ctx_->hotkeyEnabled) {
            const char* modItems[] = {"Ctrl+Shift", "Ctrl+Alt", "Alt+Shift"};
            ImGui::Combo("Modifiers", &hotkeyModSel_, modItems, 3);
            ImGui::InputText("Key", hotkeyKeyBuf_, sizeof(hotkeyKeyBuf_));
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "Current: %s+%c", modItems[hotkeyModSel_], hotkeyKeyBuf_[0]);
        }

        ImGui::Separator();
        ImGui::Checkbox("Wake word enabled", &ctx_->wakeWordEnabled);
        if (ctx_->wakeWordEnabled) {
            if (ImGui::InputText("Wake word", wakeWordBuf_, sizeof(wakeWordBuf_))) {
                ctx_->wakeWord = wakeWordBuf_;
            }
        }
    }

    // ===== Event feed =====
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Events:");
    ImGui::BeginChild("gk_events", ImVec2(0, 110), true);
    const auto& evs = mgr_->Events();
    for (auto it = evs.rbegin(); it != evs.rend(); ++it) {
        ImGui::Text("[%s] %s", GatekeeperManager::SourceName(it->source), it->description.c_str());
    }
    ImGui::EndChild();

    // ===== Routing decisions =====
    ImGui::Text("Routing decisions:");
    ImGui::BeginChild("gk_decisions", ImVec2(0, 150), true);
    const auto& decs = mgr_->Decisions();
    for (auto it = decs.rbegin(); it != decs.rend(); ++it) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "-> %s", GatekeeperManager::TargetName(it->target));
        ImGui::TextWrapped("%s", it->prompt.c_str());
        ImGui::Separator();
    }
    ImGui::EndChild();
}
