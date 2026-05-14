
/// stl
#include <memory>
#include <string>
#include <vector>

/// engine
// directX12
#include "engine/code/directX12/DxDebug.h"

/// FrameWorks
#include "application/code/__APP_NAME__Editor.h"
#include "application/code/__APP_NAME__Game.h"
#include "FrameWork.h"

/// externals
#include "logger/Logger.h"

std::vector<std::string> ParseCommandLine();

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#if defined(_DEBUG) || defined(DEBUG_REPLAY)
    OriGine::DxDebug::GetInstance()->InitializeDebugger();
#endif

    std::vector<std::string> cmdLines = ParseCommandLine();

    OriGine::Logger::Initialize();

    std::unique_ptr<FrameWork> application = nullptr;

#if defined(DEBUG) || defined(DEBUG_REPLAY)
    application = std::make_unique<__APP_NAME__Editor>();
#else
    application = std::make_unique<__APP_NAME__Game>();
#endif

    application->Initialize(cmdLines);

#if defined(_DEBUG) || defined(DEBUG_REPLAY)
    OriGine::DxDebug::GetInstance()->CreateInfoQueue();
#endif

    application->Run();

    application->Finalize();
    OriGine::Logger::Finalize();

    return 0;
}

std::vector<std::string> ParseCommandLine() {
    int argc     = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(sizeNeeded - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), sizeNeeded, nullptr, nullptr);
        args.push_back(std::move(arg));
    }
    LocalFree(argv);

    return args;
}
