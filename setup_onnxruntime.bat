@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

rem ==========================================================================
rem ONNX Runtime セットアップスクリプト
rem ==========================================================================
rem Microsoft 公式プレビルド (Windows x64) をダウンロードし、
rem project\application\externals\onnxruntime\ に include / lib / bin を配置する。
rem Windows 10/11 同梱の curl.exe と tar.exe のみを使用。
rem
rem 使い方:
rem   setup_onnxruntime.bat                 (CPU, 既定版数)
rem   setup_onnxruntime.bat 1.22.0          (CPU, 版数指定)
rem   setup_onnxruntime.bat 1.26.0 gpu      (GPU/CUDA 版)
rem ==========================================================================

set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=1.22.0"

set "VARIANT=%~2"
if /I "%VARIANT%"=="gpu" (
    set "PKG=onnxruntime-win-x64-gpu-%VERSION%"
    set "KIND=GPU/CUDA"
) else (
    set "PKG=onnxruntime-win-x64-%VERSION%"
    set "KIND=CPU"
)

set "URL=https://github.com/microsoft/onnxruntime/releases/download/v%VERSION%/%PKG%.zip"
set "ROOT=%~dp0"
set "EXT=%ROOT%project\application\externals\onnxruntime"
set "TMPZIP=%TEMP%\%PKG%.zip"
set "TMPDIR=%TEMP%\onnxruntime_extract"

echo === ONNX Runtime %VERSION% (%KIND%) ===
echo Downloading: %URL%

curl -fL --retry 3 -o "%TMPZIP%" "%URL%"
if errorlevel 1 (
    echo ダウンロード失敗: %URL%
    echo 版数が存在するか確認してください。
    pause
    exit /b 1
)

echo Extracting...
if exist "%TMPDIR%" rmdir /S /Q "%TMPDIR%"
mkdir "%TMPDIR%"
tar -xf "%TMPZIP%" -C "%TMPDIR%"
if errorlevel 1 ( echo 展開に失敗しました & pause & exit /b 1 )

set "SRC=%TMPDIR%\%PKG%"

rem 既存の externals をクリーンに作り直す
if exist "%EXT%" rmdir /S /Q "%EXT%"
mkdir "%EXT%\include"
mkdir "%EXT%\lib"
mkdir "%EXT%\bin"

rem include: ヘッダ一式 / lib: .lib / bin: .dll (curl externals と同じ構成)
xcopy /E /I /Y /Q "%SRC%\include" "%EXT%\include" >nul
for %%f in ("%SRC%\lib\*.lib") do copy /Y "%%f" "%EXT%\lib" >nul
for %%f in ("%SRC%\lib\*.dll") do copy /Y "%%f" "%EXT%\bin" >nul

del /Q "%TMPZIP%"
rmdir /S /Q "%TMPDIR%"

echo.
echo ONNX Runtime を %EXT% に配置しました。
echo 次に premake.ps1 を実行してプロジェクトを再生成してください。
pause
