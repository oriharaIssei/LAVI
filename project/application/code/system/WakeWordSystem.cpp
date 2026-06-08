#include "WakeWordSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"

#include "winApp/WinApp.h"
#include "Engine.h"

WakeWordSystem::WakeWordSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Input) {} // ユーザー入力（発話のウェイクワード）処理

WakeWordSystem::~WakeWordSystem() = default;

void WakeWordSystem::Update() {
    SharedMediaContext& ctx = LaviContext::Get();

    if(!(ctx.wakeWordEnabled && !ctx.wakeWord.empty() &&
         !ctx.transcribedText.empty() && ctx.transcribedText != lastWakeWordText_)){
        return;
    }

    // 前回処理分との差分のみを判定対象にする（転写は追記的に伸びる）。
    std::string newPart;
    if(lastWakeWordText_.empty() || ctx.transcribedText.size() <= lastWakeWordText_.size()){
        newPart = ctx.transcribedText;
    } else{
        newPart = ctx.transcribedText.substr(lastWakeWordText_.size());
    }
    lastWakeWordText_ = ctx.transcribedText;

    if(newPart.find(ctx.wakeWord) != std::string::npos){
        auto* wa = OriGine::Engine::GetInstance()->GetWinApp();
        if(wa->IsMinimizedToTray()){
            wa->RestoreFromTray();
        } else{
            ::ShowWindow(wa->GetHwnd(),SW_RESTORE);
            ::SetForegroundWindow(wa->GetHwnd());
        }
    }
}
