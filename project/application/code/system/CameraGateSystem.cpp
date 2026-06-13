#include "CameraGateSystem.h"

#include "CameraGatekeeper.h"
#include "GatekeeperManager.h" // GateSource, Config
#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturePromptComponent.h"

#include "mediaCapture/WebCamera.h" // OriGine::WebCamera 完全定義
#include "util/StringUtil.h"        // ConvertString

#include <future>
#include <memory>

CameraGateSystem::CameraGateSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {}

CameraGateSystem::~CameraGateSystem() = default;

void CameraGateSystem::Initialize() {
    camera_ = std::make_unique<CameraGatekeeper>();
    LaviContext::Get().cameraGate = camera_.get(); // GatekeeperPanel のチューニング用に公開
    lastEval_ = std::chrono::steady_clock::now();

    // JSON 設定から検出パラメータを適用（Release でも反映）。
    const GatekeeperConfigData cfg = LoadGatekeeperConfig();
    camera_->SetConfidenceThreshold(cfg.camThreshold);
    camera_->SetDetectionParams(cfg.scaleFactor, cfg.minNeighbors, cfg.minFaceSize);
    camera_->SetConfirmFrames(cfg.confirmFrames);
    camera_->SetIgnoreNeutral(cfg.ignoreNeutral);
    camera_->SetNeutralBias(cfg.neutralBias);

    // カメラ有効時のみ FER/Haar モデルを起動時ロードする（既定 OFF＝不要なロードコストを避ける）。
    // DEBUG パネルの「Load Models」ボタンでも個別にロード可。
    if (cfg.camEnabled && !cfg.ferModelPath.empty()) {
        if (camera_->Initialize(ConvertString(cfg.ferModelPath), cfg.haarPath, cfg.camUseGpu)) {
            camera_->ResetState();
        }
    }
}

void CameraGateSystem::Finalize() {
    if (evalFuture_.valid()) {
        evalFuture_.wait(); // 進行中のワーカーが camera_ を使い終わるのを待つ
    }
    evalBusy_ = false;
    LaviContext::Get().cameraGate = nullptr;
    camera_.reset();
}

void CameraGateSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();
    GatekeeperManager* gk   = ctx.gkManager; // 設定参照（GatekeeperSystem が公開）
    if (!gk || !camera_) return;

    // 1) 完了した非同期評価を回収し、結果を反映してトリガー時に CapturePrompt を発行する。
    if (evalBusy_ && evalFuture_.valid() &&
        evalFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        EmotionResult r = evalFuture_.get();
        evalBusy_       = false;
        ctx.camResult   = r;
        if (camera_->ShouldTrigger(r)) {
            auto e = CreateEntity("CapturePrompt");
            AddComponent<CapturePromptComponent>(e);
            if (auto* c = GetComponent<CapturePromptComponent>(e)) {
                c->source      = static_cast<int>(GateSource::Camera);
                c->description = std::string("表情が ") + CameraGatekeeper::EmotionName(r.dominant) + " に変化";
                c->time        = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            }
        }
    }

    // 2) 新しい評価を起動（busy でない・有効・間隔経過・未評価の新フレーム）。
    const auto& cfg = gk->config();
    if (!cfg.camEnabled || !camera_->IsReady()) return;
    if (evalBusy_) return;

    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<float>(now - lastEval_).count() < cfg.camInterval) return;

    auto frame = ctx.cameraFrame; // 不変スナップショットを掴む（ワーカーが安全に読める）
    if (!frame || frame->pixels.empty() || frame->width == 0 || frame->height == 0) return;
    if (frame->seq == lastEvaluatedSeq_) return; // 同一フレームを二度評価しない

    lastEval_         = now;
    lastEvaluatedSeq_ = frame->seq;
    ctx.camFrameW     = frame->width;
    ctx.camFrameH     = frame->height;

    CameraGatekeeper* cam = camera_.get();
    evalBusy_             = true;
    evalFuture_ = std::async(std::launch::async, [cam, frame]() -> EmotionResult {
        EmotionResult r = cam->Evaluate(frame->pixels.data(), frame->width, frame->height);
        r.frameSeq      = frame->seq; // 顔矩形とフレームの整合確認用
        return r;
    });
}
