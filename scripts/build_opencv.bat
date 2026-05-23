@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

rem ==========================================================================
rem OpenCV 静的 /MT ビルドスクリプト
rem ==========================================================================
rem OpenCV を「静的ライブラリ・/MT・最小モジュール (core/imgproc/objdetect)」で
rem ビルドし、project\application\externals\opencv\ に include / lib を配置する。
rem
rem 静的リンクにすることで exe と OpenCV が単一モジュール (単一ヒープ) になり、
rem /MT アプリと std::vector を安全に受け渡せる (DLL 境界のヒープ問題を回避)。
rem
rem 使い方:
rem   build_opencv.bat            (既定版数 4.13.0)
rem   build_opencv.bat 4.13.0     (版数指定)
rem ==========================================================================

set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=4.13.0"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
set CMAKE_EXE="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

set "ROOT=%~dp0..\"
set "EXTROOT=%ROOT%project\application\externals"
set "SRCZIP=%EXTROOT%\opencv-%VERSION%.zip"
set "SRCDIR=%EXTROOT%\opencv-%VERSION%"
set "INSTALL=%EXTROOT%\opencv"

rem --- ソース取得 ---
if not exist "%SRCDIR%" (
    echo === Downloading OpenCV %VERSION% source ===
    curl -fL --retry 3 -o "%SRCZIP%" "https://github.com/opencv/opencv/archive/refs/tags/%VERSION%.zip"
    if errorlevel 1 ( echo ソースのダウンロード失敗 & pause & exit /b 1 )
    echo === Extracting source ===
    tar -xf "%SRCZIP%" -C "%EXTROOT%"
    if errorlevel 1 ( echo 展開失敗 & pause & exit /b 1 )
    del /Q "%SRCZIP%"
)

rem --- 共通 CMake オプション (静的・最小・余計な依存を全て無効化) ---
set COMMON_OPTS=-G Ninja ^
 -DBUILD_SHARED_LIBS=OFF ^
 -DBUILD_opencv_world=OFF ^
 -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
 -DBUILD_LIST=core,imgproc,objdetect ^
 -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF ^
 -DBUILD_opencv_apps=OFF -DBUILD_DOCS=OFF -DBUILD_JAVA=OFF ^
 -DWITH_IPP=OFF -DWITH_ITT=OFF -DWITH_TBB=OFF -DWITH_OPENMP=OFF ^
 -DWITH_OPENCL=OFF -DWITH_CUDA=OFF -DWITH_EIGEN=OFF -DWITH_PROTOBUF=OFF ^
 -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF -DWITH_MSMF=OFF -DWITH_DSHOW=OFF ^
 -DWITH_QUIRC=OFF -DWITH_ADE=OFF -DWITH_1394=OFF ^
 -DBUILD_PNG=OFF -DBUILD_JPEG=OFF -DBUILD_WEBP=OFF -DBUILD_TIFF=OFF ^
 -DBUILD_OPENJPEG=OFF -DBUILD_JASPER=OFF -DBUILD_OPENEXR=OFF ^
 -DBUILD_ZLIB=ON ^
 -DCMAKE_INSTALL_PREFIX="%INSTALL%" ^
 -DOPENCV_INCLUDE_INSTALL_PATH=include ^
 -DOPENCV_LIB_INSTALL_PATH=lib ^
 -DOPENCV_3P_LIB_INSTALL_PATH=lib ^
 -DOPENCV_CONFIG_INSTALL_PATH=cmake ^
 -DOPENCV_OTHER_INSTALL_PATH=etc

rem --- Release (/MT) ---
echo === Configuring Release (/MT) ===
%CMAKE_EXE% -S "%SRCDIR%" -B "%SRCDIR%\build_mt_release" %COMMON_OPTS% -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
if errorlevel 1 ( echo CMake configure Release failed & pause & exit /b 1 )
%CMAKE_EXE% --build "%SRCDIR%\build_mt_release" -j
if errorlevel 1 ( echo Build Release failed & pause & exit /b 1 )
%CMAKE_EXE% --install "%SRCDIR%\build_mt_release"
if errorlevel 1 ( echo Install Release failed & pause & exit /b 1 )
echo === Release build succeeded ===

rem --- Debug (/MTd) ---
echo === Configuring Debug (/MTd) ===
%CMAKE_EXE% -S "%SRCDIR%" -B "%SRCDIR%\build_mt_debug" %COMMON_OPTS% -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug
if errorlevel 1 ( echo CMake configure Debug failed & pause & exit /b 1 )
%CMAKE_EXE% --build "%SRCDIR%\build_mt_debug" -j
if errorlevel 1 ( echo Build Debug failed & pause & exit /b 1 )
%CMAKE_EXE% --install "%SRCDIR%\build_mt_debug"
if errorlevel 1 ( echo Install Debug failed & pause & exit /b 1 )
echo === Debug build succeeded ===

rem --- Haar カスケード (顔検出用) を同梱 ---
mkdir "%INSTALL%\haarcascades" 2>nul
copy /Y "%SRCDIR%\data\haarcascades\haarcascade_frontalface_default.xml" "%INSTALL%\haarcascades" >nul

echo.
echo === All builds succeeded ===
echo OpenCV (static /MT) を %INSTALL% に配置しました。
echo lib フォルダの .lib 名を確認後、premake5.lua のリンク設定を行います。
echo 次に premake.ps1 を実行してプロジェクトを再生成してください。
dir /B "%INSTALL%\lib"
pause
