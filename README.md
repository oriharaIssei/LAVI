# LAVI — Live in AI

OriGine Engine ベースの AI 統合 Windows デスクトップアプリケーション。
マイク・Webカメラ・画面キャプチャの入力に対して、リアルタイムに AI 処理を行います。

## 機能

| 機能 | 技術 | 概要 |
|------|------|------|
| 音声認識 | [whisper.cpp](https://github.com/ggerganov/whisper.cpp) + CUDA | マイク入力をリアルタイム文字起こし (beam search + VAD) |
| 音声合成 | [VoiceVox](https://voicevox.hiroshiba.jp/) Engine | テキストから音声を生成・再生 (XAudio2) |
| 画像認識 | Claude API (Vision) | Webカメラ/画面キャプチャを Claude に送信して解析 |
| メディアキャプチャ | DirectX 12 / WASAPI | マイク・Webカメラ・画面キャプチャの統合管理 |

## 必要環境

- Windows 10/11 (x64)
- Visual Studio 2026 (v145 ツールセット)
- NVIDIA GPU + [CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit) (Whisper GPU推論用)
- [OriGine Engine](https://github.com/oriharaIssei/OriGine) (git submodule)

## ビルド手順

```powershell
# 1. submodule 取得
git submodule update --init --recursive

# 2. ソリューション生成
.\premake.ps1

# 3. Whisper CUDA ビルド (初回のみ)
.\build_whisper_cuda.bat

# 4. Visual Studio で project/LAVI.slnx を開いてビルド (Debug / Develop / Release)
```

## リソースの準備

ビルド前に以下を配置してください (いずれも git 管理対象外)。

### Whisper モデル

`project/application/resource/whisper/` に配置:

- `ggml-large-v3.bin` — 認識モデル
- `ggml-silero-v6.2.0.bin` — VAD モデル

### VoiceVox Engine

`project/application/resource/voiceVox/` に VoiceVox Engine の 7z を配置し、
`resource/7zip/7z.exe` で展開:

```powershell
.\project\application\resource\7zip\7z.exe x `
    .\project\application\resource\voiceVox\voicevox_engine-windows-nvidia-*.7z.001 `
    -o.\project\application\resource\voiceVox\
```

### libcurl

`project/application/externals/curl/` に以下の構成で配置:

```
curl/
├── include/curl/   # ヘッダー (curl.h 等)
├── lib/            # libcurl.lib (MSVC インポートライブラリ)
└── bin/            # libcurl.dll
```

## ディレクトリ構成

```
LAVI/
├── premake.ps1                 # premake 実行ラッパ
├── build_whisper_cuda.bat      # Whisper CUDA ビルドスクリプト
├── Docs/
│   └── Todo.html
└── project/
    ├── config/
    │   └── premake5.lua        # ワークスペース定義
    ├── engine/                 # submodule: OriGine Engine
    └── application/
        ├── code/
        │   ├── main.cpp
        │   ├── FrameWork.{h,cpp}
        │   ├── LaviEditor.{h,cpp}    # Debug 用エディタ
        │   ├── LaviGame.{h,cpp}      # Release 用ゲームループ
        │   └── system/
        │       ├── MediaCaptureDemoSystem.{h,cpp}  # メディア統合 UI
        │       ├── WhisperTranscriber.{h,cpp}      # 音声認識
        │       ├── VoiceVoxClient.{h,cpp}          # 音声合成
        │       └── VisionAnalyzer.{h,cpp}          # 画像認識
        ├── externals/
        │   ├── whisper.cpp/    # whisper.cpp (submodule)
        │   ├── curl/           # libcurl (手動配置)
        │   └── stb_image_write.h
        └── resource/
            ├── whisper/        # Whisper モデル (手動配置)
            ├── voiceVox/       # VoiceVox Engine (7z 展開)
            ├── 7zip/           # 7-Zip コマンドライン版
            └── api_config.json # API キー設定 (自動生成)
```

## 使い方

1. アプリを起動すると **Media Capture Demo** ウィンドウが開きます
2. 各タブから機能を利用:
   - **Microphone** — マイクの選択・録音・Whisper による文字起こし
   - **WebCamera** — Webカメラ映像のプレビュー
   - **ScreenCapture** — 画面キャプチャのプレビュー
   - **VoiceVox** — テキスト入力 → 音声合成・再生、Whisper 結果の読み上げ
   - **Vision** — Webカメラ/画面キャプチャのフレームを Claude API で解析

## ライセンス

本リポジトリのアプリケーションコードは個人プロジェクトです。
利用している外部ライブラリ・エンジンはそれぞれのライセンスに従います。
