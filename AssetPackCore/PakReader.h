#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include "Keys.h"
#include "PakFormat.h"

namespace AssetPack
{
	// .cpak を開いて仮想パス単位で読み出す。将来的に Mesh::Load / Texture::LoadInternal
	// から呼ばれる想定 (現状は未接続。呼び出し側の統合は別途)。
	//
	// 想定される使い方 (今回は実装のみで engine 側には未配線):
	//   AssetPack::PakReader pak;
	//   if (pak.Open("Assets.cpak", key) && pak.Has(path)) {
	//       auto bytes = pak.Read(path);
	//       importer.ReadFileFromMemory(bytes.data(), bytes.size(), flags, hintExt);
	//   } else {
	//       // 開発中はルーズファイルへフォールバック
	//   }
	class PakReader
	{
	public:
		bool Open(const std::string& pakPath, const Key32& key);
		void Close();
		bool IsOpen() const { return m_file.is_open(); }

		bool Has(const std::string& virtualPath) const;
		size_t FileCount() const { return m_index.size(); }

		// 見つからない場合は空の vector を返す。呼び出し側は Has() で事前確認するか
		// 戻り値が空かどうかで判定する。
		std::vector<uint8_t> Read(const std::string& virtualPath);

	private:
		std::ifstream m_file;
		Key32 m_key{};
		std::unordered_map<uint64_t, PakEntry> m_index;
	};
}
