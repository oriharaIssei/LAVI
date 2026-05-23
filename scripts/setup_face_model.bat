@echo off
setlocal

set DEST_DIR=%~dp0..\project\application\resource\gatekeeper
set DEST_FILE=%DEST_DIR%\arcface-resnet100.onnx

echo === ArcFace Face Recognition Model Setup ===

if exist "%DEST_FILE%" (
    echo Already exists: %DEST_FILE%
    pause
    exit /b 0
)

echo Downloading ArcFace ResNet-100 (approx 249MB)...
echo.

REM GitHub LFS files require media.githubusercontent.com
set URL=https://media.githubusercontent.com/media/onnx/models/main/validated/vision/body_analysis/arcface/model/arcfaceresnet100-11.onnx

echo Trying: media.githubusercontent.com ...
powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%URL%' -OutFile '%DEST_FILE%' -UseBasicParsing" 2>nul

if exist "%DEST_FILE%" (
    for %%A in ("%DEST_FILE%") do set FILE_SIZE=%%~zA
) else (
    set FILE_SIZE=0
)

if %FILE_SIZE% LSS 10000000 (
    echo First method failed. Trying curl...
    if exist "%DEST_FILE%" del "%DEST_FILE%"
    curl -L -o "%DEST_FILE%" "%URL%"
)

if exist "%DEST_FILE%" (
    for %%A in ("%DEST_FILE%") do set FILE_SIZE=%%~zA
) else (
    set FILE_SIZE=0
)

if %FILE_SIZE% LSS 10000000 (
    echo.
    echo ============================================================
    echo   Auto download failed.
    echo   Please download manually:
    echo.
    echo   1. Open: https://github.com/onnx/models/tree/main/validated/vision/body_analysis/arcface/model
    echo   2. Download: arcfaceresnet100-11.onnx
    echo   3. Rename to: arcface-resnet100.onnx
    echo   4. Place in: %DEST_DIR%
    echo ============================================================
    if exist "%DEST_FILE%" del "%DEST_FILE%"
    pause
    exit /b 1
)

echo.
echo Done: %DEST_FILE% (%FILE_SIZE% bytes)
pause
