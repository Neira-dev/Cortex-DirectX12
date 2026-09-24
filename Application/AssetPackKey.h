#pragma once
#include "../AssetPackCore/Keys.h"

// Cortex(このゲーム)固有の pak 復号鍵。AssetPackCore::DefaultKey() はライブラリ側の
// 汎用プレースホルダ (ソースに公開されている) であり、これをそのまま配布ビルドで
// 使うと暗号化の意味がない。実際の配布ビルドを作る際の手順:
//
//   1. AssetPacker.exe --generate-key project.key  (32byte の乱数鍵を新規生成)
//   2. project.key の中身 (32byte) をこのファイルの kProjectKey にコピーする
//   3. project.key をどこか安全な場所に保管し、リポジトリにはコミットしない
//      (.gitignore 等で除外すること)
//   4. AssetPacker --input Assets --output Assets.cpak --key-file project.key で
//      パックする (kProjectKey と同じ鍵でなければ配布ビルドは pak を復号できない)
//
// TemplateMain (Template.cpp) はこの GetProjectPakKey() を AssetPack::TryOpenGlobalPak
// に明示的に渡すので、AssetPackCore::DefaultKey() の既定引数には依存しない。
// kProjectKey が未変更 (=AssetPackCore::DefaultKey() と同じ) のままだと、
// Release ビルド起動時に警告が出る (Template.cpp 参照)。
namespace AppAssetPack
{
	// 既定値は「未カスタマイズ」を示すプレースホルダとして AssetPackCore::DefaultKey() と
	// 意図的に一致させてある。配布前に必ずこの配列を実際の鍵バイト列へ書き換えること。
	inline const AssetPack::Key32& GetProjectPakKey()
	{
		static const AssetPack::Key32 kProjectKey = AssetPack::DefaultKey();
		return kProjectKey;
	}
}
