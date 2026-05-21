#include "ScreenGatekeeper.h"

#define ENGINE_INCLUDE
#define ENGINE_PROCESS_MANAGER
#include <EngineInclude.h>

#include "util/StringUtil.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {
constexpr int kDiffW = 320;
constexpr int kDiffH = 180;
}  // namespace

ScreenGatekeeper::ScreenGatekeeper() = default;
ScreenGatekeeper::~ScreenGatekeeper() = default;

ScreenGateResult ScreenGatekeeper::Evaluate(const uint8_t* bgra, uint32_t width, uint32_t height) {
    ScreenGateResult result;

    // --- フォアグラウンドアプリの切り替え ---
    OriGine::WindowProcessInfo fg = OriGine::ProcessManager::GetForegroundApp();
    result.foregroundApp = ConvertString(fg.exeName);
    result.foregroundTitle = ConvertString(fg.windowTitle);

    if (watchForeground_) {
        const uint64_t key = reinterpret_cast<uint64_t>(fg.hwnd);
        if (hasForeground_ && key != lastForegroundKey_) {
            result.foregroundChanged = true;
        }
        lastForegroundKey_ = key;
        hasForeground_ = true;
    }

    // --- 新規ウィンドウ (新規起動) ---
    if (detectNew_) {
        std::unordered_set<std::string> current;
        for (const auto& w : OriGine::ProcessManager::EnumerateWindows()) {
            std::string exe = ConvertString(w.exeName);
            if (exe.empty()) continue;
            if (hasProcessBaseline_ && knownProcesses_.find(exe) == knownProcesses_.end() &&
                current.find(exe) == current.end()) {
                result.newApps.push_back(exe);
            }
            current.insert(std::move(exe));
        }
        knownProcesses_ = std::move(current);
        hasProcessBaseline_ = true;
    }

    // --- 画面差分 ---
    if (bgra && width > 0 && height > 0) {
        cv::Mat frame(static_cast<int>(height), static_cast<int>(width), CV_8UC4,
                      const_cast<uint8_t*>(bgra));
        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
        cv::Mat small;
        cv::resize(gray, small, cv::Size(kDiffW, kDiffH));

        if (!prevGray_.empty() && prevW_ == kDiffW && prevH_ == kDiffH) {
            cv::Mat prev(kDiffH, kDiffW, CV_8UC1, prevGray_.data());
            cv::Mat diff;
            cv::absdiff(small, prev, diff);
            cv::Mat mask;
            cv::threshold(diff, mask, static_cast<double>(pixelDiffThreshold_), 255.0,
                          cv::THRESH_BINARY);
            const int changed = cv::countNonZero(mask);
            result.screenDiffRatio = static_cast<float>(changed) / (kDiffW * kDiffH);
            result.screenChanged = result.screenDiffRatio > screenDiffThreshold_;
        }

        prevGray_.assign(small.data, small.data + static_cast<size_t>(kDiffW) * kDiffH);
        prevW_ = kDiffW;
        prevH_ = kDiffH;
    }

    result.triggered =
        result.foregroundChanged || !result.newApps.empty() || result.screenChanged;
    return result;
}

void ScreenGatekeeper::ResetState() {
    lastForegroundKey_ = 0;
    hasForeground_ = false;
    knownProcesses_.clear();
    hasProcessBaseline_ = false;
    prevGray_.clear();
    prevW_ = prevH_ = 0;
}
