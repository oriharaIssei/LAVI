#include "VoiceSettingsSystem.h"

#include "LaviContext.h"
#include "SharedMediaContext.h"
#include "VoiceVoxClient.h"
#include "system/component/ui/UIComboComponent.h"

#include "component/ComponentArray.h"
#include "globalVariables/GlobalVariables.h"

#include <string>

namespace {
const std::string kScene = "Settings";
const std::string kGroup = "VoiceVox";
constexpr const char* kKeySpeaker = "SelectedSpeaker";
} // namespace

VoiceSettingsSystem::VoiceSettingsSystem()
    : OriGine::ISystem(OriGine::SystemCategory::StateTransition) {}

VoiceSettingsSystem::~VoiceSettingsSystem() = default;

void VoiceSettingsSystem::Initialize() {
    // 保存済みの話者インデックスを復元（話者一覧が未取得でも int として保持し、使用時にクランプ）。
    auto* gv = OriGine::GlobalVariables::GetInstance();
    gv->LoadFile(kScene, kGroup);
    const int saved = *gv->AddValue<int>(kScene, kGroup, kKeySpeaker, 0);
    LaviContext::Get().selectedSpeaker = (saved < 0) ? 0 : saved;
}

void VoiceSettingsSystem::Finalize() {}

void VoiceSettingsSystem::Update() {
    auto* arr = GetComponentArray<UIComboComponent>();
    if (!arr) {
        return;
    }
    SharedMediaContext& ctx = LaviContext::Get();
    const int n = static_cast<int>(ctx.voiceVoxSpeakers.size());

    for (auto& slot : arr->GetSlotsRef()) {
        for (auto& combo : slot.components) {
            if (combo.role != "voicevox.speaker") {
                continue;
            }

            // 選択肢: 「話者名 (スタイル名)」。話者未取得時は空（プレースホルダ表示）。
            combo.items.clear();
            combo.items.reserve(static_cast<size_t>(n));
            for (const auto& sp : ctx.voiceVoxSpeakers) {
                combo.items.push_back(sp.name + "（" + sp.styleName + "）");
            }

            if (combo.requestedIndex >= 0 && combo.requestedIndex < n) {
                ctx.selectedSpeaker = combo.requestedIndex;

                auto* gv = OriGine::GlobalVariables::GetInstance();
                gv->SetValue(kScene, kGroup, kKeySpeaker, ctx.selectedSpeaker);
                gv->SaveFile(kScene, kGroup);

                combo.selectedIndex  = combo.requestedIndex;
                combo.requestedIndex = -1;
            } else {
                // 現在の選択に追従（範囲外は 0 にクランプ）。
                int idx = ctx.selectedSpeaker;
                if (idx < 0 || idx >= n) idx = (n > 0) ? 0 : -1;
                combo.selectedIndex = idx;
            }
        }
    }
}
