#include "TurnController.h"

#include <fstream>
#include <nlohmann/json.hpp>

bool TurnController::LoadConfig(const std::string& path) {
    path_ = path;
    std::ifstream file(path);
    if (!file.is_open()) {
        return false; // 既定値のまま
    }
    try {
        nlohmann::json j;
        file >> j;
        config_.enabled         = j.value("enabled", config_.enabled);
        config_.bargeInEnabled  = j.value("bargeInEnabled", config_.bargeInEnabled);
        config_.startThreshold  = j.value("startThreshold", config_.startThreshold);
        config_.endThreshold    = j.value("endThreshold", config_.endThreshold);
        config_.minSpeechMs     = j.value("minSpeechMs", config_.minSpeechMs);
        config_.silenceHangoverMs = j.value("silenceHangoverMs", config_.silenceHangoverMs);
        config_.bargeInMs       = j.value("bargeInMs", config_.bargeInMs);
    } catch (...) {
        return false;
    }
    return true;
}

void TurnController::SaveConfig() const {
    if (path_.empty()) return;
    nlohmann::json j;
    j["enabled"]           = config_.enabled;
    j["bargeInEnabled"]    = config_.bargeInEnabled;
    j["startThreshold"]    = config_.startThreshold;
    j["endThreshold"]      = config_.endThreshold;
    j["minSpeechMs"]       = config_.minSpeechMs;
    j["silenceHangoverMs"] = config_.silenceHangoverMs;
    j["bargeInMs"]         = config_.bargeInMs;
    std::ofstream file(path_);
    if (file.is_open()) {
        file << j.dump(2);
    }
}

void TurnController::Update(float rms, float dtSeconds, bool laviSpeaking) {
    if (!config_.enabled) return;

    lastRms_ = rms;
    const float dtMs = dtSeconds * 1000.0f;

    switch (state_) {
    case TurnState::Idle:
        // 開始しきい値を一定時間超え続けたら発話開始
        if (rms >= config_.startThreshold) {
            speechRunMs_ += dtMs;
            if (speechRunMs_ >= config_.minSpeechMs) {
                state_        = TurnState::UserSpeaking;
                silenceRunMs_ = 0.0f;
                if (onStart_) onStart_();
            }
        } else {
            speechRunMs_ = 0.0f;
        }
        break;

    case TurnState::UserSpeaking:
        // 終了しきい値を下回る無音が一定時間続いたら発話終了（文の途中の短い無音では切らない）
        if (rms < config_.endThreshold) {
            silenceRunMs_ += dtMs;
            if (silenceRunMs_ >= config_.silenceHangoverMs) {
                state_       = TurnState::Processing;
                speechRunMs_ = 0.0f;
                if (onEnd_) onEnd_();
            }
        } else {
            silenceRunMs_ = 0.0f;
        }
        break;

    case TurnState::LaviSpeaking:
        // LAVI 発話中にユーザーが話し始めたら割り込み（barge-in）
        if (config_.bargeInEnabled && laviSpeaking && rms >= config_.startThreshold) {
            speechRunMs_ += dtMs;
            if (speechRunMs_ >= config_.bargeInMs) {
                state_        = TurnState::UserSpeaking;
                speechRunMs_  = 0.0f;
                silenceRunMs_ = 0.0f;
                if (onBargeIn_) onBargeIn_();
                if (onStart_) onStart_();
            }
        } else {
            speechRunMs_ = 0.0f;
        }
        break;

    case TurnState::Processing:
        // 応答待ち。発話検知はせず、外部通知（NotifyResponseStarted/Reset）で遷移する。
        break;
    }
}

void TurnController::NotifyResponseStarted() {
    state_        = TurnState::LaviSpeaking;
    speechRunMs_  = 0.0f;
    silenceRunMs_ = 0.0f;
}

void TurnController::NotifyResponseEnded() {
    if (state_ == TurnState::LaviSpeaking) {
        state_ = TurnState::Idle;
    }
    speechRunMs_  = 0.0f;
    silenceRunMs_ = 0.0f;
}

void TurnController::Reset() {
    state_        = TurnState::Idle;
    speechRunMs_  = 0.0f;
    silenceRunMs_ = 0.0f;
}
