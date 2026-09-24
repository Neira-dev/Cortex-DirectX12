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
	// .cpak を開いて仮想パス単位で読み出す。Mesh::Load / Texture::LoadInternal から
	// GlobalPakReader() 経由で呼ばれる (GlobalPak.h)。
	//
	// 使い方:
	//   AssetPack::PakReader::ReadResult r = pak.TryRead(path, bytes);
	//   if (r == AssetPack::PakReader::ReadResult::Success) {
	//       importer.ReadFileFromMemory(bytes.data(), bytes.size(), flags, hintExt);
	//   } else if (r == AssetPack::PakReader::ReadResult::Failed) {
	//       // pak には存在するのに読み込みに失敗した = 破損の可能性が高い。
	//       // NotFound (単に pak 未収録) と区別して診断・ログに出すこと。
	//   } else {
	//       // NotFound: 開発中のルーズファイル運用などでは普通に起こる
	//   }
	class PakReader
	{
	public:
		enum class ReadResult
		{
			NotFound, // 仮想パスがこの pak に収録されていない (pak 未オープンも含め呼び出し側で判定)
			Failed,   // 収録されてはいるが読み込みに失敗した (壊れた/改ざんされた pak の可能性)
			Success,  // 読み込み成功。0 byte の正規エントリでも Success になる
		};

		bool Open(const std::string& pakPath, const Key32& key);
		void Close();
		bool IsOpen() const { return m_file.is_open(); }

		bool Has(const std::string& virtualPath) const;
		size_t FileCount() const { return m_index.size(); }

		// Has()+Read() を両方呼ぶと正規化+ハッシュ+マップ検索が二重に走る上、
		// Read() が返す空 vector だけでは「未収録」「0 byte の正規エントリ」
		// 「読み込み失敗 (破損)」を区別できない。TryRead() は検索を1回で済ませつつ
		// この3状態を ReadResult で区別できるようにしたもの。新規コードはこちらを使うこと。
		ReadResult TryRead(const std::string& virtualPath, std::vector<uint8_t>& outBytes);

		// 互換用の簡易版。内部で TryRead() を呼ぶ。NotFound/Failed のどちらも
		// 空の vector を返すため区別できない (区別が要る場合は TryRead() を使うこと)。
		std::vector<uint8_t> Read(const std::string& virtualPath);

	private:
		std::ifstream m_file;
		Key32 m_key{};
		uint64_t m_fileSize = 0;
		std::unordered_map<uint64_t, PakEntry> m_index;
	};
}
