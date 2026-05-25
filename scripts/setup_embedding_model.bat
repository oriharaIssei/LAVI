@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul

rem ==========================================================================
rem Sentence Embedding モデル配置スクリプト
rem ==========================================================================
rem multilingual-e5-small (ONNX) をダウンロードし
rem project\application\resource\embedding\ へ配置する。
rem vocab.txt は HuggingFace の tokenizer.json から変換する。
rem ==========================================================================

set "ROOT=%~dp0..\"
set "RESDIR=%ROOT%project\application\resource\embedding"
set "HF_BASE=https://huggingface.co/intfloat/multilingual-e5-small/resolve/main"

if not exist "%RESDIR%" mkdir "%RESDIR%"

rem --- ONNX モデル ---
if exist "%RESDIR%\model.onnx" (
    echo model.onnx は既にあります。スキップします。
) else (
    echo === Downloading multilingual-e5-small ONNX model ===
    curl -fL --retry 3 -o "%RESDIR%\model.onnx" "%HF_BASE%/onnx/model.onnx"
    if errorlevel 1 (
        echo モデルのダウンロード失敗。直接URLを試します...
        curl -fL --retry 3 -o "%RESDIR%\model.onnx" "https://huggingface.co/intfloat/multilingual-e5-small/resolve/main/onnx/model.onnx"
        if errorlevel 1 ( echo ダウンロード失敗 & pause & exit /b 1 )
    )
)

rem --- tokenizer.json ---
if exist "%RESDIR%\tokenizer.json" (
    echo tokenizer.json は既にあります。スキップします。
) else (
    echo === Downloading tokenizer.json ===
    curl -fL --retry 3 -o "%RESDIR%\tokenizer.json" "%HF_BASE%/tokenizer.json"
    if errorlevel 1 ( echo tokenizer.json のダウンロード失敗 & pause & exit /b 1 )
)

rem --- vocab.txt を tokenizer.json から生成 ---
echo === Converting tokenizer.json to vocab.txt ===
powershell -ExecutionPolicy Bypass -Command ^
    "$j = Get-Content '%RESDIR%\tokenizer.json' -Raw | ConvertFrom-Json; " ^
    "$vocab = $j.model.vocab; " ^
    "$out = @(); " ^
    "foreach ($prop in $vocab.PSObject.Properties) { $out += $prop.Name + [char]9 + $prop.Value }; " ^
    "$out | Out-File -Encoding utf8 '%RESDIR%\vocab.txt' -Force"
if errorlevel 1 ( echo vocab.txt 変換失敗 & pause & exit /b 1 )

echo.
echo 配置完了: %RESDIR%
dir /B "%RESDIR%"
echo.
echo モデルサイズ:
for %%F in ("%RESDIR%\model.onnx") do echo   model.onnx: %%~zF bytes
pause
