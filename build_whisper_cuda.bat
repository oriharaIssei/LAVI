@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2
set PATH=%CUDA_PATH%\bin;%PATH%

set CMAKE_EXE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set SRC_DIR=C:\Users\ira34\source\repos\OriGine-Dev\project\application\externals\whisper.cpp

set COMMON_OPTS=-G Ninja -DGGML_CUDA=ON -DCUDAToolkit_ROOT="%CUDA_PATH%" -DCMAKE_CUDA_COMPILER="%CUDA_PATH%\bin\nvcc.exe" -DBUILD_SHARED_LIBS=OFF -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_SERVER=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW

echo === Building Release ===
%CMAKE_EXE% -S "%SRC_DIR%" -B "%SRC_DIR%\build_cuda_release" %COMMON_OPTS% -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if %errorlevel% neq 0 ( echo CMake configure Release failed & pause & exit /b 1 )
%CMAKE_EXE% --build "%SRC_DIR%\build_cuda_release" --config Release -j
if %errorlevel% neq 0 ( echo Build Release failed & pause & exit /b 1 )
echo === Release build succeeded ===

echo === Building Debug ===
%CMAKE_EXE% -S "%SRC_DIR%" -B "%SRC_DIR%\build_cuda_debug" %COMMON_OPTS% -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug
if %errorlevel% neq 0 ( echo CMake configure Debug failed & pause & exit /b 1 )
%CMAKE_EXE% --build "%SRC_DIR%\build_cuda_debug" --config Debug -j
if %errorlevel% neq 0 ( echo Build Debug failed & pause & exit /b 1 )
echo === Debug build succeeded ===

echo === All builds succeeded ===
pause
