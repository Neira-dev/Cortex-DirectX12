#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Keys.h"

namespace AssetPack
{
	// .cpak を1つ組み立てて書き出す。使い方:
	//   PakWriter w;
	//   w.AddFile("Assets/Models/Character.fbx", bytes, MakeTypeTag('F','B','X',' '));
	//   w.WriteTo("Assets.cpak", key);
	class PakWriter
	{
	public:
		// virtualPath は PathHash::Hash64 で正規化されるので大文字小文字・区切り文字の
		// 表記ゆれは気にしなくてよい。同じ正規化パスを 2 回 AddFile すると
		// WriteTo 時にエラーになる (ハッシュ衝突検出も兼ねる)。
		void AddFile(const std::string& virtualPath, std::vector<uint8_t> bytes, uint32_t typeTag);

		size_t FileCount() const { return m_entries.size(); }

		// 失敗時 (パス重複 / ファイル書き込み失敗) は false を返す。
		bool WriteTo(const std::string& outPakPath, const Key32& key) const;

	private:
		struct PendingEntry
		{
			std::string          virtualPath;
			std::vector<uint8_t> bytes;
			uint32_t              typeTag;
		};
		std::vector<PendingEntry> m_entries;
	};
}
