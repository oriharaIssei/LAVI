#include "LocationSystem.h"

#include "LocationProvider.h"
#include "LaviContext.h"
#include "SharedMediaContext.h"

LocationSystem::LocationSystem()
    : OriGine::ISystem(OriGine::SystemCategory::Initialize) {}

LocationSystem::~LocationSystem() = default;

void LocationSystem::Initialize() {
    location_ = std::make_unique<LocationProvider>();
    location_->Start(); // バックグラウンドで現在地取得を開始
    LaviContext::Get().location = location_.get(); // 共有状態に公開（消費側は LaviContext 経由）
}

void LocationSystem::Finalize() {
    LaviContext::Get().location = nullptr;
    location_.reset();
}
