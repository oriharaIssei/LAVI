-- ==========================================================================
-- OriGineDev Workspace Premake5 Script
-- ==========================================================================
-- このファイルはテンプレートから生成されています。
-- - Application 固有の premake 記述のみをここに書く
-- - Engine 側の project 定義 (OriGine / DirectXTex / imgui 等) は
--   engine リポジトリ (submodule) 内の premake から include する想定
-- ==========================================================================

-- 先頭で作業ディレクトリを project に変更
os.chdir(_SCRIPT_DIR .. "/..")

-- Engine 側のヘルパを読み込み (Engine 側で premake.lua を提供する)
-- 例: Engine repo のルート直下に premake.lua を置き、
--     defineEngineProjects() / getEngineIncludeDirs() / getEngineLinks() を export する
include "engine/premake.lua"

workspace "OriGineDev"
    architecture "x86_64"
    configurations { "Debug", "Develop", "Release" }
    startproject "OriGineDev"

-- ==========================================================================
-- Engine Projects (Engine 側の premake.lua が定義する)
-- ==========================================================================
defineEngineProjects()

-- ==========================================================================
-- whisper.cpp (CMake で CUDA 付きビルド済み)
-- ==========================================================================
-- ビルド手順: build_whisper_cuda.bat を実行
-- 生成物: application/externals/whisper.cpp/build_cuda/ 以下の .lib

-- ==========================================================================
-- Application Project
-- ==========================================================================
project "OriGineDev"
    kind "WindowedApp"
    language "C++"
    location "application"
    targetdir "../generated/output/%{cfg.buildcfg}/"
    objdir "../generated/obj/%{cfg.buildcfg}/OriGineDev/"
    debugdir "%{wks.location}"
    files { "application/**.h", "application/**.cpp" }
    removefiles { "application/externals/**" }

    clangtidy "On"

    includedirs(table.join(
        {
            "$(ProjectDir)code",
            "$(ProjectDir)",
            "$(SolutionDir)application/externals/whisper.cpp/include",
            "$(SolutionDir)application/externals/whisper.cpp/ggml/include",
            "$(SolutionDir)application/externals/curl/include",
        },
        getEngineIncludeDirs()
    ))

    dependson { "DirectXTex", "imgui" }
    libdirs { "$(CUDA_PATH)/lib/x64" }
    libdirs { "application/externals/curl/lib" }
    links(table.join(
        { "OriGine", "whisper", "ggml", "ggml-base", "ggml-cpu", "ggml-cuda", "cudart_static", "cublas", "cublasLt", "cuda",
          "libcurl", "xaudio2" },
        getEngineLinks()
    ))

    libdirs { "application/externals/curl/lib" }
    filter {}

    filter "configurations:Debug"
        libdirs {
            "application/externals/whisper.cpp/build_cuda_debug/src",
            "application/externals/whisper.cpp/build_cuda_debug/ggml/src",
            "application/externals/whisper.cpp/build_cuda_debug/ggml/src/ggml-cuda",
        }
    filter "configurations:Develop or Release"
        libdirs {
            "application/externals/whisper.cpp/build_cuda_release/src",
            "application/externals/whisper.cpp/build_cuda_release/ggml/src",
            "application/externals/whisper.cpp/build_cuda_release/ggml/src/ggml-cuda",
        }
    filter {}

    warnings "Extra"
    multiprocessorcompile "On"
    buildoptions { "/utf-8", "/bigobj" }

    filter "configurations:Debug"
        defines { "DEBUG", "_DEBUG" }
        symbols "On"
        runtime "Debug"
        libdirs { "engine/externals/assimp/lib/Debug" }
        links { "assimp-vc143-mtd" }
        staticruntime "On"

    filter "configurations:Develop"
        defines { "DEVELOP", "_DEVELOP" }
        symbols "On"
        runtime "Release"
        libdirs { "engine/externals/assimp/lib/Release" }
        links { "assimp-vc143-mt" }
        staticruntime "On"

    filter "configurations:Release"
        defines { "NDEBUG", "_RELEASE", "RELEASE" }
        optimize "Full"
        runtime "Release"
        libdirs { "engine/externals/assimp/lib/Release" }
        links { "assimp-vc143-mt" }
        staticruntime "On"

    filter "system:windows"
        cppdialect "C++20"
        systemversion "latest"
        postbuildcommands {
            '{COPY} "%{wks.location}/application/externals/curl/bin/libcurl.dll" "%{cfg.targetdir}"',
        }
