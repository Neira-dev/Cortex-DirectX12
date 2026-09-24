# Cortex

Windows / C++20 / DirectX 12 + llama.cpp のデスクトップアプリ。
ビルドは **Visual Studio 2022 の `Cortex.sln`**、外部依存は **git submodule** で管理する（NuGet は不使用）。

## 目次

- [プロジェクト構成](#プロジェクト構成)
- [セットアップ](#セットアップ)
  - [1. 必要なもの](#1-必要なもの)
  - [2. submodule を取得](#2-submodule-を取得)
  - [3. ビルド & 実行](#3-ビルド--実行)
- [LocalLLM / llama.cpp のビルド](#localllm--llamacpp-のビルド)
  - [事前条件：ASCII のみのパスに置く](#事前条件ascii-のみのパスに置く)
  - [ビルド手順](#ビルド手順)
  - [有効化](#有効化)
- [新しい依存を追加するには](#新しい依存を追加するには)
- [トラブルシューティング](#トラブルシューティング)

---

## プロジェクト構成

| プロジェクト | 種別 | 説明 |
|---|---|---|
| `Application` | 実行ファイル (WinMain) | エントリポイント。他プロジェクトを束ねる |
| `Windows`     | 静的ライブラリ | Win32 GUI ラッパ (`Window` クラス) |
| `DirectX12`   | 静的ライブラリ | D3D12 描画 |
| `Helper`      | 静的ライブラリ | 文字コード変換等のユーティリティ |
| `LocalLLM`    | 静的ライブラリ | LLM 呼び出し。`llamaServer`(WinHTTP) と `BaseLLM`(llama.cpp) |
| `Thread`      | 静的ライブラリ | ワーカースレッド |

---

## セットアップ

### 1. 必要なもの

| ツール | 用途 |
|---|---|
| Visual Studio 2022 (v143) + 「C++ によるデスクトップ開発」 | 本体ビルド |
| Windows 10/11 SDK | D3D12 |
| Git | submodule 取得 |
| CMake 3.24+ | （`LocalLLM/llama.cpp` を使う場合のみ） |
| NVIDIA CUDA Toolkit | （llama.cpp を GPU で動かす場合のみ） |

### 2. submodule を取得

```powershell
git clone https://github.com/PIGUMI/Cortex.git
cd Cortex
git submodule update --init --recursive
```

または `powershell -ExecutionPolicy Bypass -File scripts\setup.ps1`（取得のみなら数十秒）。

| submodule | 用途 | ビルド |
|---|---|---|
| `ThirdParty/json` | `<nlohmann/json.hpp>` | 不要（ヘッダオンリー） |
| `ThirdParty/DirectX-Headers` | `<d3d12.h>` / `<d3dx12.h>`（旧 `Microsoft.Direct3D.D3D12` NuGet 相当） | 不要（ヘッダオンリー） |
| `LocalLLM/llama.cpp` | ローカル LLM 推論（`BaseLLM`） | **既定では不要**（[下記参照](#localllm--llamacpp-のビルド)） |
| `Curl/curl` | 予約（現状ソース未使用） | 不要 |

インクルードパスはリポジトリ直下の **`Directory.Build.props`** が全 `.vcxproj` へ自動で通すため、
プロジェクトファイルの個別設定は不要。

### 3. ビルド & 実行

1. Visual Studio で `Cortex.sln` を開く
2. 構成を **`Release` / `x64`** にする
3. スタートアッププロジェクトを **`Application`** にして F5

> [!NOTE]
> この状態では `LocalLLM` はサーバー経由の呼び出し（`llamaServer` / WinHTTP）のみ有効。
> `LocalLLM/llama.cpp` のビルドは不要。

---

## LocalLLM / llama.cpp のビルド

**ローカル推論を使う場合のみ**必要な手順。

`BaseLLM` クラス（`LocalLLM/BaseLLM.cpp`）を使う場合だけ必要。
既定では `Directory.Build.props` の `CortexUseLlama=false` により `BaseLLM.cpp` はビルドされず、
`Application` は llama ライブラリをリンクしない。

### 事前条件：ASCII のみのパスに置く

> [!WARNING]
> **CMake（4.0.x）は、パスに非 ASCII 文字（日本語など）が含まれると Visual Studio ジェネレーターで
> 即クラッシュする**（`0xC0000409`、出力なし）。
> `C:\Users\<名前>\OneDrive\デスクトップ\...` のようなパスでは llama.cpp をビルドできない。

対処のいずれか:

- **リポジトリを `C:\dev\Cortex` などへ移動する**（推奨。OneDrive 配下に `build/` を置く問題も回避）
- 一時的に ASCII ドライブを割り当てる:
  ```powershell
  subst X: "C:\Users\<名前>\OneDrive\デスクトップ\Project"
  cd X:\Cortex
  # ここで下記のビルドを実行。終わったら subst X: /D
  ```
  （CMake キャッシュに `X:` パスが記録されるため、以後の `cmake --build` 時も `X:` を割り当てておくこと）

### ビルド手順

```powershell
# CPU のみ
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -Llama

# CUDA（要 CUDA Toolkit / nvcc）
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 -Llama -Cuda
```

`scripts\setup.ps1 -Llama` がやること:
1. `git submodule update --init --recursive`
2. `LocalLLM/llama.cpp` を CMake（`Visual Studio 17 2022` / `x64` / `Release` / `BUILD_SHARED_LIBS=ON`）でビルド
   → `llama.lib` / `ggml*.lib` / `llama-common*.lib` と実行時 DLL を生成

オプション: `-BuildCurl`（curl も静的ビルド）, `-Clean`（build ディレクトリを削除して再構成）
`-Llama` を付けないと submodule 取得のみで終了する。

手動で行う場合:

```powershell
cmake -S LocalLLM/llama.cpp -B LocalLLM/llama.cpp/build -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_SHARED_LIBS=ON -DGGML_CUDA=OFF -DLLAMA_CURL=OFF `
  -DLLAMA_BUILD_COMMON=ON -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF `
  -DLLAMA_BUILD_TOOLS=OFF -DLLAMA_BUILD_SERVER=OFF
cmake --build LocalLLM/llama.cpp/build --config Release --parallel
```

### 有効化

`Directory.Build.props` を編集:

```xml
<CortexUseLlama Condition="'$(CortexUseLlama)' == ''">true</CortexUseLlama>
<!-- CUDA ビルドした場合はこちらも true -->
<CortexLlamaCuda Condition="'$(CortexLlamaCuda)' == ''">true</CortexLlamaCuda>
```

または `msbuild Cortex.sln /p:Configuration=Release /p:Platform=x64 /p:CortexUseLlama=true`。

`Application` はビルド後に `LocalLLM/llama.cpp/build/bin/Release/*.dll` を出力フォルダへコピーする
（`Application.vcxproj` の `CopyLlamaRuntimeDlls` ターゲット）。

---

## 新しい依存を追加するには

```powershell
# ヘッダオンリーの場合
git submodule add --depth 1 https://github.com/<owner>/<repo>.git ThirdParty/<name>
#  → Directory.Build.props の <AdditionalIncludeDirectories> に $(CortexRoot)ThirdParty\<name>\include を追加

git commit -am "deps: add <name>"
```

CMake ライブラリの場合は `scripts\setup.ps1` にビルド手順を足し、
生成 `.lib` のパスを `Directory.Build.props`（または対象 `.vcxproj`）の
`<AdditionalLibraryDirectories>` / `<AdditionalDependencies>` に追加する（llama.cpp が実例）。

submodule のコミットを更新:

```powershell
cd ThirdParty/<name>; git fetch; git checkout <tag>; cd ../..
git add ThirdParty/<name>; git commit -m "deps: bump <name> to <tag>"
```

---

## トラブルシューティング

| 症状 | 対処 |
|---|---|
| CMake が出力なしで即終了（`0xC0000409`） | パスに日本語 → ASCII パスへ移動 or `subst`（[上記参照](#事前条件ascii-のみのパスに置く)） |
| `d3d12.h` の再定義エラー | `Directory.Build.props` が DirectX-Headers 版 `d3d12.h` を SDK 版より優先している（旧 Agility SDK と同じ）。SDK 版に戻すなら `...\include\directx` の行を削除し、ソースを `#include <directx/d3dx12.h>` に変更 |
| `llama.lib` が見つからない (LNK1104) | `CortexUseLlama=true` なのに llama.cpp 未ビルド。`scripts\setup.ps1` を `Release/x64` で実行 |
| `nvcc` が無い | `scripts\setup.ps1 -Llama`（`-Cuda` を付けない）。`CortexLlamaCuda` は `false` のままに |
| submodule が空 | `git submodule update --init --recursive` |
| `scripts\setup.ps1` が文字化けエラー | UTF-8 BOM 付きで保存し直す（Windows PowerShell 5.1 対策）か `pwsh` で実行 |
