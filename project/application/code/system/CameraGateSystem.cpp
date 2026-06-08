#include "CameraGateSystem.h"

#include "CameraGatekeeper.h"
#include "GatekeeperManager.h" // GateSource, Config
#include "GatekeeperConfig.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "system/component/CapturePromptComponent.h"

#include "mediaCapture/WebCamera.h" // OriGine::WebCamera 完全定義
#include "util/StringUtil.h"        // ConvertString

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
    LaviContext::Get().cameraGate = nullptr;
    camera_.reset();
}

void CameraGateSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();
    GatekeeperManager* gk = ctx.gkManager; // 設定参照（GatekeeperSystem が公開）
    if(!gk || !camera_) return;

    const auto& cfg = gk->config();
    if(!cfg.camEnabled || !camera_->IsReady()) return;

    const auto now = std::chrono::steady_clock::now();
    if(std::chrono::duration<float>(now - lastEval_).count() < cfg.camInterval) return;
    lastEval_ = now;

    if(!(ctx.webCamera && ctx.webCamera->IsCapturing())) return;
    uint32_t fw = 0, fh = 0;
    if(!(ctx.webCamera->GetLatestFrame(ctx.camFrameBuffer, fw, fh) && fw > 0 && fh > 0)) return;

    ctx.camFrameW = fw;
    ctx.camFrameH = fh;
    ctx.camResult = camera_->Evaluate(ctx.camFrameBuffer.data(), fw, fh);

    if(camera_->ShouldTrigger(ctx.camResult)){
        auto e = CreateEntity("CapturePrompt");
        AddComponent<CapturePromptComponent>(e);
        if(auto* c = GetComponent<CapturePromptComponent>(e)){
            c->source = static_cast<int>(GateSource::Camera);
            c->description = std::string("表情が ") + CameraGatekeeper::EmotionName(ctx.camResult.dominant) + " に変化";
            c->time = std::chrono::duration<double>(now.time_since_epoch()).count();
        }
    }
}
