#include "PakWriter.h"
#include "PakFormat.h"
#include "PathHash.h"
#include "Chacha20.h"
#include <fstream>
#include <random>
#include <unordered_set>
#include <algorithm>

namespace AssetPack
{
	void PakWriter::AddFile(const std::string& virtualPath, std::vector<uint8_t> bytes, uint32_t typeTag)
	{
		m_entries.push_back(PendingEntry{ virtualPath, std::move(bytes), typeTag });
	}

	bool PakWriter::WriteTo(const std::string& outPakPath, const Key32& key)
	{
		// パスハッシュの重複チェック (異なる仮想パスが同じハッシュに落ちる事故を検出)。
		// 出力ファイルを開く前に行う: ここで失敗した場合に outPakPath の既存ファイルを
		// (trunc で) 巻き込みで壊さないため。各エントリのハッシュはここで1回だけ計算し、
		// 後段のインデックス構築で使い回す (二重計算の回避)。
		std::vector<uint64_t> hashes;
		hashes.reserve(m_entries.size());
		{
			std::unordered_set<uint64_t> seen;
			seen.reserve(m_entries.size());
			for (const auto& e : m_entries)
			{
				const uint64_t h = Hash64(e.virtualPath);
				if (!seen.insert(h).second)
				{
					return false;
				}
				hashes.push_back(h);
			}
		}

		std::ofstream out(outPakPath, std::ios::binary | std::ios::trunc);
		if (!out) return false;

		const uint32_t entryCount = static_cast<uint32_t>(m_entries.size());
		const uint64_t indexOffset = sizeof(PakHeader);
		const uint64_t dataStart = indexOffset + static_cast<uint64_t>(entryCount) * sizeof(PakEntry);

		PakHeader header{};
		header.magic[0] = kPakMagic[0]; header.magic[1] = kPakMagic[1];
		header.magic[2] = kPakMagic[2]; header.magic[3] = kPakMagic[3];
		header.version = kPakVersion;
		header.entryCount = entryCount;
		header.flags = 0;
		header.indexOffset = indexOffset;

		std::random_device rd;

		// 1st pass: メタデータのみ (バイト列本体には触れない) を組み立ててヘッダー+
		// インデックスを先に書き出す。
		std::vector<PakEntry> indexEntries;
		indexEntries.reserve(m_entries.size());
		uint64_t runningOffset = dataStart;
		for (size_t i = 0; i < m_entries.size(); ++i)
		{
			const auto& e = m_entries[i];
			PakEntry pe{};
			pe.pathHash = hashes[i];
			pe.dataOffset = runningOffset;
			pe.dataSize = e.bytes.size();
			pe.typeTag = e.typeTag;
			pe.flags = kPakFlagEncrypted;
			for (auto& b : pe.nonce) b = static_cast<uint8_t>(rd() & 0xFF);
			runningOffset += pe.dataSize;
			indexEntries.push_back(pe);
		}

		out.write(reinterpret_cast<const char*>(&header), sizeof(header));
		for (const auto& pe : indexEntries)
		{
			out.write(reinterpret_cast<const char*>(&pe), sizeof(pe));
		}

		// 2nd pass: 各エントリを in-place で暗号化して即座に書き出す。平文コピー+暗号文
		// コピーを同時に保持しないので、全アセット分を二重に確保しない
		// (ピークメモリは元の平文サイズのまま)。
		for (size_t i = 0; i < m_entries.size(); ++i)
		{
			auto& bytes = m_entries[i].bytes;
			if (!bytes.empty())
			{
				Chacha20Xor(key.data(), indexEntries[i].nonce, 0, bytes.data(), bytes.size());
				out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			}
		}

		return static_cast<bool>(out);
	}
}
