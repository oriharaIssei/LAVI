# ==========================================================================
# Application Setup Script
# --------------------------------------------------------------------------
# 新規プロジェクトのセットアップを自動化します。
#   1. __APP_NAME__ プレースホルダをアプリ名に置換
#   2. Engine リポジトリを git submodule として追加
#   3. premake を実行してソリューションを生成
#
# 使い方:
#   .\setup.ps1 -AppName "MyGame" -EngineRepo "https://github.com/<user>/Engine.git"
#
# オプション:
#   -SkipSubmodule  : Engine の submodule 追加をスキップ
#   -SkipPremake    : premake 実行をスキップ
# ==========================================================================

param(
    [Parameter(Mandatory = $true)]
    [string]$AppName,

    [Parameter(Mandatory = $false)]
    [string]$EngineRepo = "",

    [Parameter(Mandatory = $false)]
    [string]$EngineBranch = "main",

    [switch]$SkipSubmodule,
    [switch]$SkipPremake
)

$ErrorActionPreference = "Stop"
$rootDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# --------------------------------------------------------------------------
# 1. 入力検証
# --------------------------------------------------------------------------
if ($AppName -notmatch '^[A-Za-z][A-Za-z0-9_]*$') {
    Write-Host "Error: AppName は英字始まりの識別子のみ使用可 (例: MyGame)" -ForegroundColor Red
    exit 1
}

Write-Host "=== __APP_NAME__ -> $AppName で置換開始 ===" -ForegroundColor Cyan

# --------------------------------------------------------------------------
# 2. ファイル中の __APP_NAME__ を置換
# --------------------------------------------------------------------------
$targetExtensions = @('*.cpp', '*.h', '*.hpp', '*.lua', '*.md', '*.ps1', '*.txt', '*.json', '*.ini', '*.yml', '*.yaml')
$excludeDirs = @('\.git', 'generated', 'project\\engine')

Get-ChildItem -Path $rootDir -Recurse -File -Include $targetExtensions | Where-Object {
    $path = $_.FullName
    -not ($excludeDirs | Where-Object { $path -match $_ })
} | ForEach-Object {
    $file = $_.FullName
    if ($file -eq $MyInvocation.MyCommand.Path) { return }

    $content = Get-Content -Path $file -Raw -Encoding UTF8
    if ($content -match '__APP_NAME__') {
        $newContent = $content -replace '__APP_NAME__', $AppName
        Set-Content -Path $file -Value $newContent -Encoding UTF8 -NoNewline
        Write-Host "  edit: $($file.Substring($rootDir.Length + 1))"
    }
}

# --------------------------------------------------------------------------
# 3. ファイル名中の __APP_NAME__ をリネーム
# --------------------------------------------------------------------------
Get-ChildItem -Path $rootDir -Recurse -File | Where-Object {
    $_.Name -match '__APP_NAME__' -and
    ($excludeDirs | Where-Object { $_.FullName -match $_ }).Count -eq 0
} | Sort-Object -Property { $_.FullName.Length } -Descending | ForEach-Object {
    $newName = $_.Name -replace '__APP_NAME__', $AppName
    $newPath = Join-Path $_.DirectoryName $newName
    Write-Host "  rename: $($_.Name) -> $newName"
    Move-Item -Path $_.FullName -Destination $newPath -Force
}

Write-Host "=== 置換完了 ===" -ForegroundColor Green

# --------------------------------------------------------------------------
# 4. Engine を submodule として追加
# --------------------------------------------------------------------------
if (-not $SkipSubmodule) {
    if ([string]::IsNullOrEmpty($EngineRepo)) {
        Write-Host "Warning: -EngineRepo が未指定のため submodule 追加をスキップ" -ForegroundColor Yellow
    } else {
        Write-Host "=== Engine submodule を追加 ===" -ForegroundColor Cyan
        $enginePath = Join-Path $rootDir "project\engine"
        if (Test-Path $enginePath) {
            Write-Host "  既に project\engine が存在するためスキップ" -ForegroundColor Yellow
        } else {
            Push-Location $rootDir
            try {
                git submodule add -b $EngineBranch $EngineRepo "project/engine"
                git submodule update --init --recursive
            } finally {
                Pop-Location
            }
        }
    }
}

# --------------------------------------------------------------------------
# 5. premake を実行
# --------------------------------------------------------------------------
if (-not $SkipPremake) {
    Write-Host "=== premake 実行 ===" -ForegroundColor Cyan
    $premakePs1 = Join-Path $rootDir "premake.ps1"
    if (Test-Path $premakePs1) {
        & $premakePs1
    } else {
        Write-Host "  premake.ps1 が見つからないためスキップ" -ForegroundColor Yellow
    }
}

Write-Host "=== セットアップ完了 ===" -ForegroundColor Green
Write-Host "次のステップ:"
Write-Host "  1. project\$AppName.sln を開く"
Write-Host "  2. ビルド構成を選択してビルド"
