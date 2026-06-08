# mtmd (libmtmd) — LAVI 向けに「ライブラリのみ」へトリミング。
# 上流 tools/mtmd/CMakeLists.txt から CLI 実行ファイル・llama-common 依存・install を除外し、
# プロセス内ビジョン推論に必要な mtmd 静的ライブラリだけをビルドする。
# 依存は ggml / llama / Threads と vendor(stb_image.h, miniaudio.h) のみ（common 非依存）。

find_package(Threads REQUIRED)

file(GLOB MTMD_MODEL_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/models/*.cpp)

add_library(mtmd
            mtmd.cpp
            mtmd-audio.cpp
            mtmd-image.cpp
            mtmd-helper.cpp
            clip.cpp
            ${MTMD_MODEL_SRCS}
            )

target_link_libraries     (mtmd PUBLIC  ggml llama)
target_link_libraries     (mtmd PRIVATE Threads::Threads)
target_include_directories(mtmd PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mtmd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_include_directories(mtmd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor)
target_compile_features   (mtmd PRIVATE cxx_std_17)

if (NOT MSVC)
    # stb_image.h / miniaudio.h
    target_compile_options(mtmd PRIVATE -Wno-cast-qual)
endif()
