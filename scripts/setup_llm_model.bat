@echo off
setlocal

set MODEL_DIR=%~dp0..\project\application\resource\llm\models
set MODEL_FILE=Qwen3-4B-Q4_K_M.gguf
set MODEL_PATH=%MODEL_DIR%\%MODEL_FILE%

if exist "%MODEL_PATH%" (
    echo Model already exists: %MODEL_PATH%
    pause
    exit /b 0
)

if not exist "%MODEL_DIR%" mkdir "%MODEL_DIR%"

echo Downloading %MODEL_FILE% from Qwen/Qwen3-4B-GGUF ...
echo This may take a few minutes (~2.5GB^).

curl -L -o "%MODEL_PATH%" "https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf"

if %ERRORLEVEL% neq 0 (
    echo Download failed.
    if exist "%MODEL_PATH%" del "%MODEL_PATH%"
    pause
    exit /b 1
)

echo Done: %MODEL_PATH%
pause
