# Application-Template

OriGine Engine を使用したアプリケーションプロジェクトの雛形です。
Engine を submodule として取り込み、最小構成の FrameWork / main.cpp / premake 設定を提供します。

---

## ディレクトリ構成

```
Application-Template/
├── README.md
├── .gitignore
├── .gitmodules              # Engine submodule 用 (setup.ps1 が記述)
├── setup.ps1                # 初回セットアップ (名前置換・submodule追加・premake)
├── premake.ps1              # premake 実行ラッパ
└── project/
    ├── config/
    │   └── premake5.lua     # workspace + App project 定義 (Engine は include)
    ├── engine/              # submodule: Engine リポジトリ (setup.ps1 で追加)
    │   └── premake.lua.example   # Engine 側に用意すべき premake.lua の参考
    └── application/
        ├── code/
        │   ├── main.cpp
        │   ├── FrameWork.{h,cpp}
        │   ├── __APP_NAME__Editor.{h,cpp}
        │   ├── __APP_NAME__Game.{h,cpp}
        │   ├── component/ComponentTemplate.txt
        │   ├── system/SystemTemplate.txt
        │   └── manager/
        └── resource/
        # cookedResource/ は AssetCooker による成果物のためローカル生成 (gitignore)
```

`__APP_NAME__` は `setup.ps1` 実行時にプロジェクト名に置換されます。

---

## クイックスタート

### 1. GitHub の Template Repository 機能で新規作成

GitHub 上で本リポジトリを **Template** に設定し、`Use this template` から新規リポジトリを生成するか、
コマンドラインから:

```powershell
gh repo create MyGame --template <user>/Application-Template --private --clone
cd MyGame
```

### 2. セットアップ実行

```powershell
.\setup.ps1 -AppName "MyGame" -EngineRepo "https://github.com/<user>/Engine.git"
```

`setup.ps1` は次を行います:
1. `__APP_NAME__` をファイル内容・ファイル名の両方で `MyGame` に置換
2. Engine を `project/engine` に git submodule として追加
3. `premake.ps1` を実行して Visual Studio ソリューションを生成

### 3. ビルド

`project/MyGame.sln` を Visual Studio で開き、`Debug` / `Develop` / `Release` のいずれかをビルド。

---

## Engine リポジトリ側で必要な準備

本テンプレートが想定する Engine 構成:

```
Engine/
├── premake.lua              # defineEngineProjects / getEngineIncludeDirs / getEngineLinks を export
├── code/                    # Engine 本体ソース
├── math/ util/ editor/ tool/
├── externals/               # DirectXTex, imgui, assimp 等
└── ...
```

`premake.lua` の実装例は [OriGine Engine リポジトリ](https://github.com/oriharaIssei/OriGine) の
ルート `premake.lua` を参照してください。
Engine 側に `premake.lua` を配置すると、Application 側の `project/config/premake5.lua` から
`include "engine/premake.lua"` 経由で Engine/DirectXTex/imgui の project 定義を再利用できます。

---

## setup.ps1 のオプション

| オプション         | 説明                                                   |
| ------------------ | ------------------------------------------------------ |
| `-AppName`         | **必須** 識別子形式のアプリ名 (例: `MyGame`)           |
| `-EngineRepo`      | Engine リポジトリの URL                                 |
| `-EngineBranch`    | Engine の追跡ブランチ (既定: `main`)                   |
| `-SkipSubmodule`   | submodule 追加をスキップ                               |
| `-SkipPremake`     | premake 実行をスキップ                                 |

---

## よくあるフロー

### 既存の OriGine リポジトリから Engine 分離後、初めて使う

1. Engine リポジトリを作成し、`premake.lua` を `premake.lua.example` を参考に配置
2. 本テンプレートから新規リポジトリを生成
3. `setup.ps1` を実行

### Engine 側を最新化したい

```powershell
cd project\engine
git pull origin main
cd ..\..
git add project/engine
git commit -m "Update Engine submodule"
```

---

## TODO / 既知の制約

- [ ] Engine 側の `premake.lua` 実装 (テンプレート側では `.example` のみ提供)
- [ ] premake5.exe の配置方法 (Engine 側 externals に入れるか、setup で取得するか要検討)
- [ ] `resource/` の初期アセット一式 (`cookedResource/` は AssetCooker 生成物のため対象外)
