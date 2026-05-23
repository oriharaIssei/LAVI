@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

rem ==========================================================================
rem ゲートキーパー用モデル配置スクリプト
rem ==========================================================================
rem FER+ 表情認識モデル (ONNX) をダウンロードし、OpenCV の Haar カスケードと
rem 共に project\application\resource\gatekeeper\ へ配置する。
rem 実行時の作業ディレクトリ (project\) からの相対パスで読み込む想定。
rem
rem 前提: 先に build_opencv.bat を実行して Haar XML が
rem       externals\opencv\haarcascades\ にあること。
rem ==========================================================================

set "ROOT=%~dp0..\"
set "RESDIR=%ROOT%project\application\resource\gatekeeper"
set "HAARSRC=%ROOT%project\application\externals\opencv\haarcascades\haarcascade_frontalface_default.xml"
set "FER_URL=https://github.com/onnx/models/raw/main/validated/vision/body_analysis/emotion_ferplus/model/emotion-ferplus-8.onnx"

if not exist "%RESDIR%" mkdir "%RESDIR%"

echo === Downloading FER+ model ===
curl -fL --retry 3 -o "%RESDIR%\emotion-ferplus-8.onnx" "%FER_URL%"
if errorlevel 1 ( echo FER+ モデルのダウンロード失敗 & pause & exit /b 1 )

echo === Copying Haar cascade ===
if exist "%HAARSRC%" (
    copy /Y "%HAARSRC%" "%RESDIR%" >nul
) else (
    echo [注意] %HAARSRC% が見つかりません。GitHub から取得します...
    curl -fL --retry 3 -o "%RESDIR%\haarcascade_frontalface_default.xml" ^
        "https://raw.githubusercontent.com/opencv/opencv/4.13.0/data/haarcascades/haarcascade_frontalface_default.xml"
)

echo.
echo 配置完了: %RESDIR%
dir /B "%RESDIR%"
pause
