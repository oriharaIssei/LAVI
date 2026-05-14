-- ==========================================================================
-- __APP_NAME__ Workspace Premake5 Script
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

workspace "__APP_NAME__"
    architecture "x86_64"
    configurations { "Debug", "Develop", "Release" }
    startproject "__APP_NAME__"

-- ==========================================================================
-- Engine Projects (Engine 側の premake.lua が定義する)
-- ==========================================================================
defineEngineProjects()

-- ==========================================================================
-- Application Project
-- ==========================================================================
project "__APP_NAME__"
    kind "WindowedApp"
    language "C++"
    location "application"
    targetdir "../generated/output/%{cfg.buildcfg}/"
    objdir "../generated/obj/%{cfg.buildcfg}/__APP_NAME__/"
    debugdir "%{wks.location}"
    files { "application/**.h", "application/**.cpp" }

    clangtidy "On"

    includedirs(table.join(
        {
            "$(ProjectDir)code",
            "$(ProjectDir)",
        },
        getEngineIncludeDirs()
    ))

    dependson { "DirectXTex", "imgui" }
    links(table.join(
        { "OriGine" },
        getEngineLinks()
    ))

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
