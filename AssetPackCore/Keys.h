#pragma once
#include <cstdint>
#include <string>
#include <array>

namespace AssetPack
{
	using Key32 = std::array<uint8_t, 32>;

	// 既定鍵。--key-file を指定しなかった場合にパッカー/リーダー双方が使う。
	//
	// 重要: これは実行ファイルに埋め込まれるため、リバースエンジニアリングされれば
	// 誰でも読み取れる。「わざわざ抽出しようとする第三者」を止める効力は無い。
	// 効くのは「配布フォルダを開いたら .fbx/.png がそのまま見える/zip で開けば読める」
	// という一番手軽な流用を防ぐところまで。本番タイトルで配布する場合は、
	// --key-file で自前の鍵ファイル (32 byte, GenerateRandomKey で作成可) を指定し、
	// このファイルの既定値には依存しないこと。
	const Key32& DefaultKey();

	// 暗号論的に安全な乱数ではなく std::random_device ベース。鍵ファイル生成用。
	Key32 GenerateRandomKey();

	// 32 byte 生バイナリの鍵ファイルを読み書きする。失敗時は false。
	bool LoadKeyFile(const std::string& path, Key32& outKey);
	bool SaveKeyFile(const std::string& path, const Key32& key);
}
