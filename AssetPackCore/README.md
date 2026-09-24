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
  - `GlobalPak.h/.cpp` — プロセス全体で共有する `PakReader` シングルトン
    (`TryOpenGlobalPak` / `IsGlobalPakOpen` / `GlobalPakReader`)。Mesh/Texture から参照される。
- **AssetPacker** (コンソール exe): `AssetPackCore` を使うビルド時ツール。
  ```
  AssetPacker.exe --input Assets --output Assets.cpak [--key-file pak.key] [--verify]
  ```
  `--input` 配下を再帰走査し、`.fbx/.obj/.gltf/.glb/.png/.jpg/.jpeg/.tga/.hdr/.dds` を
  仮想パス (例 `Assets/Models/Character.fbx`) のまま1つの `.cpak` に集約・暗号化する。
  `--verify` を付けると書き出し直後に全エントリを読み戻してバイト一致を確認する。

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

- `Mesh::Load`: `AssetPack::IsGlobalPakOpen() && GlobalPakReader().Has(FILEPATH)` が真なら
  `GlobalPakReader().Read(FILEPATH)` で得たバイト列を `Assimp::Importer::ReadFileFromMemory`
  に渡す。それ以外 (pak 未オープン、または該当パスが無い) は従来通り `ReadFile` でルーズ
  ファイルを読む。
- `Texture::LoadInternal`: 同様に pak にあれば `DirectX::LoadFromWICMemory` /
  `LoadFromHDRMemory` を、無ければ従来通り `LoadFromWICFile` / `LoadFromHDRFile` を使う。

**`AssetPack::TryOpenGlobalPak(path, key)` は呼ばれていない** — つまりこの状態では
`IsGlobalPakOpen()` が常に false のままなので、今まで通りルーズファイル運用のまま挙動は
一切変わらない。配布ビルドで pak を有効化したい場合は、起動シーケンスのどこかで一度

```cpp
AssetPack::TryOpenGlobalPak("Assets.cpak", myKey);
```

を呼ぶ処理を追加する必要がある (どこで呼ぶか、鍵をどう埋め込むかは配布フローの都合に
合わせて検討: `Application` の初期化処理が候補)。この呼び出しの追加は行っていない。

動作確認: `Manager.vcxproj` 経由のフルビルドは `ThirdParty/DirectXTex` の shader 事前生成
(`Shaders/Compiled/*.inc`) が本環境に無いため通せなかった (既知の環境依存問題、今回の変更とは
無関係)。代わりに `cl.exe /c` で `Mesh.cpp`/`Texture.cpp` を実際の include パス一式を渡して
直接コンパイルし、警告・エラーなしで通ることを確認済み。
