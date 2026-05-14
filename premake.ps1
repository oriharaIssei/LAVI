param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArgs
)

$rootDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# premake5.exe は Engine submodule (project/engine/tools/) から供給される
$premakeExe = Join-Path $rootDir "project\engine\tools\premake5.exe"
# App 側 workspace 定義は project/config/ に置く
$premakeLua = Join-Path $rootDir "project\config\premake5.lua"

if (-not (Test-Path $premakeExe)) {
    Write-Host "Error: premake5.exe not found at $premakeExe" -ForegroundColor Red
    Write-Host "  Engine submodule が初期化されているか確認してください (git submodule update --init --recursive)" -ForegroundColor Yellow
    exit 1
}

if (-not (Test-Path $premakeLua)) {
    Write-Host "Error: premake5.lua not found at $premakeLua" -ForegroundColor Red
    exit 1
}

if (-not $RemainingArgs -or $RemainingArgs.Count -eq 0) {
    $RemainingArgs = @("vs2026")
}

& $premakeExe --file=$premakeLua @RemainingArgs

if ($LASTEXITCODE -ne 0) {
    Write-Host "Premake failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "Premake completed successfully." -ForegroundColor Green
