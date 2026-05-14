#include "FrameWork.h"

/// engine include
#define ENGINE_INCLUDE
#define ENGINE_ECS
#define ENGINE_COMPONENTS
#define ENGINE_SYSTEMS
#include <EngineInclude.h>

using namespace OriGine;

FrameWork::FrameWork()  = default;
FrameWork::~FrameWork() = default;

void RegisterUsingComponents() {
    ComponentRegistry* componentRegistry = ComponentRegistry::GetInstance();
    (void)componentRegistry;

    // TODO: アプリ固有のコンポーネントを登録
    // 例:
    // componentRegistry->RegisterComponent<Transform>();
}

void RegisterUsingSystems() {
    SystemRegistry* systemRegistry = SystemRegistry::GetInstance();
    (void)systemRegistry;

    // TODO: アプリ固有のシステムを登録
    // 例:
    // systemRegistry->RegisterSystem<MoveSystem>();
}
