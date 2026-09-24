# AssetPackCore / AssetPacker

配布ビルドで `.fbx` / `.png` 等をそのまま流用されないようにするためのパッキング機構。
`Cortex.sln` に新規プロジェクトとして追加されており、既存プロジェクトへの変更は
`Manager` (下記の読み込み統合のみ) と `Cortex.sln` への登録に限定している。

## 構成

- **AssetPackCore** (静的ライブラリ): `.cpak` の読み書きロジック本体。
  - `PakFormat.h` — バイナリレイアウト (ヘッダ32byte + エントリ48byte×N + 暗号化データ本体)
  - `PathHash.h/.cpp` — 仮想パス正規化 + FNV-1a 64bit ハッシュ (パス文字列そのものはパックに保存しない)
  - `Chacha20.h/.cpp` — 外部ライブラリ非依存の自己完結 ChaCha20 (RFC 8439)
  - `Keys.h/.cpp` — 既定鍵 / 鍵ファイルの読み書き
  - `PakWriter.h/.cpp` — 複数ファイルを1つの `.cpak` へ書き出す
  - `PakReader.h/.cpp` — `.cpak` を開いて仮想パス単位でメモリに読み出す
  - `GlobalPak.h/.cpp` — プロセス全体で共有する `PakReader` を、`Mesh`/`Texture`/`Shader`
    と同じ `static Get()`/`Del()` シングルトンの流儀で管理するクラス
    (`AssetPack::GlobalPak::Get()->TryOpen(...)`/`IsOpen()`/`Reader()`)。
    Mesh.cpp/Texture.cpp から `GlobalPak::Get()` 経由で参照される。
- **AssetPacker** (コンソール exe): `AssetPackCore` を使うビルド時ツール。
  ```
  AssetPacker.exe --generate-key project.key
  AssetPacker.exe --input Assets --output Assets.cpak --key-file project.key [--verify]
  ```
  `--input` 配下を再帰走査し、`.fbx/.obj/.gltf/.glb/.png/.jpg/.jpeg/.tga/.hdr/.dds` を
  仮想パス (例 `Assets/Models/Character.fbx`) のまま1つの `.cpak` に集約・暗号化する。
  `--verify` を付けると書き出し直後に全エントリを読み戻してバイト一致を確認する。
  `--key-file` と `--allow-default-key` のどちらも指定しないとエラーで終了する
  (公開されている `DefaultKey()` のまま気づかず配布ビルドを作ってしまう事故を防ぐため)。

動作確認: 4構成 (Debug/Release × x64/Win32) すべてビルド確認済み。ダミーの `.fbx`/`.png` を
`Assets/` 配下に置いて実際にパック→`--verify`→バイナリ内に元データが残っていないこと
(`grep` で平文が見つからない、仮想パス文字列も残らない) まで確認済み。

## なぜこれで「そのまま流用」を防げるか / 防げないか

- 防げること: 配布フォルダを開いても `.fbx`/`.png` が裸で見えない。単純な zip 展開等でも
  中身は読めない (ChaCha20 で暗号化済み、仮想パスもハッシュ化されておりファイル一覧からの
  推測もできない)。
- 防げないこと: 実行時にはゲーム自身が復号して読む必要があるため、鍵は最終的に実行ファイル
  (`Keys.cpp` の `DefaultKey()`、または `--key-file` で読み込んだファイル) のどこかに
  存在する。バイナリ解析やメモリダンプを行う相手には防御にならない。「エクスプローラで
  コピーする」「アーカイブツールで開く」といった手軽な流用を防ぐのが目的であり、
  本格的な DRM ではない。

## 運用タイミング (開発中の反復とどう両立するか)

このツールは **配布ビルド直前に1回実行するだけ** を想定している。日々のモデル差し替え等の
開発イテレーションは、エンジン側が (今後実装する場合) ルーズファイルを直接読み続ける限り
pak 化とは無関係に行える。「差し替えるたびに pak 化」という運用は想定していない。

## Cortex 本体への組み込み (実施済み)

`Manager/Mesh.cpp` の `Mesh::Load` と `Manager/Texture.cpp` の `Texture::LoadInternal` を、
`AssetPack::GlobalPak` 経由で pak を優先して読むように変更済み。

- `Mesh::Load`: `AssetPack::GlobalPak::Get()->Reader().TryRead(FILEPATH, pakBytes)` の結果
  (`ReadResult::Success`/`Failed`/`NotFound`) を見て、`Success` なら
  `Assimp::Importer::ReadFileFromMemory` に渡す。`NotFound` (pak 未オープン、または
  該当パスが無い) は従来通り `ReadFile` でルーズファイルを読む。`Failed` (pak には
  あるのに読み込み自体が失敗 = 破損の可能性) はルーズファイルへフォールバックしつつ、
  `NotFound` と区別して警告ダイアログを出す。
- `Texture::LoadInternal`: 同様に `TryRead` の結果に応じて `DirectX::LoadFromWICMemory` /
  `LoadFromHDRMemory` (`Success`) か `LoadFromWICFile` / `LoadFromHDRFile` (`NotFound`) を
  使い分け、`Failed` は警告を出す。

### 鍵の配線 (実施済み)

`Application/AssetPackKey.h` にプロジェクト固有の鍵を置く場所を用意した。
`Application/Template.cpp` の `TemplateMain` 冒頭 (`/* 初期化 */` ブロックの先頭、Window
初期化より前) で `AppAssetPack::GetProjectPakKey()` を明示的に取得し、
`AssetPack::GlobalPak::Get()->TryOpen("Assets.cpak", pakKey)` へ渡す
(`AssetPackCore::DefaultKey()` の既定引数には依存しない)。

- リポジトリルート (デバッグ実行時の作業ディレクトリ) に `Assets.cpak` が無ければ `Open()` は
  false を返すだけで何も起きない → 今まで通りルーズファイル運用のまま動く。
- 実際の配布ビルドを作る手順:
  1. `AssetPacker --generate-key project.key` で32byteの乱数鍵を生成 (リポジトリには
     コミットしないこと)
  2. `project.key` の中身を `Application/AssetPackKey.h` の `GetProjectPakKey()` に
     コピーする
  3. `AssetPacker --input Assets --output Assets.cpak --key-file project.key` でパックする
- `AssetPackKey.h` の鍵が未カスタマイズ (`AssetPackCore::DefaultKey()` のまま) だと、
  Release ビルド起動時に警告ダイアログが出る (Debug では出さない)。
- `AssetPacker` は `--key-file` か `--allow-default-key` のどちらかを明示しないとエラーで
  終了するようにした。うっかり公開鍵のまま配布ビルドを作ってしまう事故を防ぐため。

動作確認: `Manager.vcxproj`/`Application.vcxproj` 経由のフルビルドは `ThirdParty/DirectXTex`
の shader 事前生成 (`Shaders/Compiled/*.inc`) が本環境に無いため通せなかった (既知の環境依存
問題、今回の変更とは無関係)。代わりに `cl.exe /c` で `Mesh.cpp`/`Texture.cpp`/`Template.cpp`
(Debug/Release 両方の `_DEBUG` 分岐) を実際の include パス一式を渡して直接コンパイルし、
警告・エラーなしで通ることを確認済み。`AssetPacker` の新しい鍵フロー
(`--generate-key` → `--key-file` でパック、`--key-file`/`--allow-default-key` 省略時のエラー、
`--allow-default-key` 指定時の動作) は実行して確認済み。

追加で確認したもの:
- `PakReader::Open()` の再オープン安全性 (失敗しても直前の有効な pak を保持したままになる)
- `PakReader::TryRead()` の3状態 (`NotFound`/`Failed`/`Success`、0byte の正規エントリと
  読み込み失敗を区別できる)
- `PakWriter::WriteTo()` を in-place 暗号化に書き換えた後もパック→検証が正しく動作し、
  出力バイナリに平文が残っていないこと (`.cpak` から元データの grep がヒットしないこと)
- `GlobalPak` を `Get()`/`Del()` シングルトンへ書き換えた後の `Mesh.cpp`/`Texture.cpp`/
  `Template.cpp` の直接コンパイル
