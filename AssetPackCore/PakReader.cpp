#include "PakReader.h"
#include "PathHash.h"
#include "Chacha20.h"
#include <cstring>
#include <limits>

namespace AssetPack
{
	bool PakReader::Open(const std::string& pakPath, const Key32& key)
	{
		// 新しい pak を完全に検証し終えるまで、既存の m_file/m_index/m_fileSize
		// (直前まで有効だった pak の状態) には一切触れない。ここで失敗して途中で
		// return しても、呼び出し側からは「Open() が false を返しただけ」に見え、
		// 直前まで有効だった pak がそのまま使い続けられる。
		std::ifstream newFile(pakPath, std::ios::binary);
		if (!newFile) return false;

		newFile.seekg(0, std::ios::end);
		const std::streamoff endPos = newFile.tellg();
		newFile.seekg(0, std::ios::beg);
		if (!newFile || endPos < 0) return false;
		const uint64_t newFileSize = static_cast<uint64_t>(endPos);

		PakHeader header{};
		newFile.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!newFile || newFile.gcount() != sizeof(header)) return false;
		if (std::memcmp(header.magic, kPakMagic, 4) != 0 || header.version != kPakVersion) return false;

		// entryCount/indexOffset はファイル由来の値なので、実ファイルサイズを超える範囲を
		// 指していないか (壊れた/改ざんされた .cpak でないか) を reserve() や read() の前に
		// 検証する。無検証だと巨大な entryCount が unordered_map::reserve で
		// length_error/bad_alloc を未捕捉のまま投げ、プロセスごと落ちる。
		if (header.indexOffset > newFileSize) return false;
		const uint64_t maxPossibleEntries = (newFileSize - header.indexOffset) / sizeof(PakEntry);
		if (header.entryCount > maxPossibleEntries) return false;

		newFile.seekg(static_cast<std::streamoff>(header.indexOffset), std::ios::beg);
		std::unordered_map<uint64_t, PakEntry> newIndex;
		newIndex.reserve(header.entryCount);
		for (uint32_t i = 0; i < header.entryCount; ++i)
		{
			PakEntry entry{};
			newFile.read(reinterpret_cast<char*>(&entry), sizeof(entry));
			if (!newFile || newFile.gcount() != sizeof(entry)) return false;
			// dataOffset/dataSize も同様にファイル範囲外を指していないか検証する。
			// (Read() 側でも確認するが、ここで弾いておけば壊れた pak を早期に拒否できる)
			if (entry.dataOffset > newFileSize || entry.dataSize > newFileSize - entry.dataOffset) return false;
			newIndex.emplace(entry.pathHash, entry);
		}

		// ここまで来れば新しい pak は完全に有効だと確認できたので、初めて既存の状態を
		// 置き換える。
		Close();
		m_file = std::move(newFile);
		m_fileSize = newFileSize;
		m_index = std::move(newIndex);
		m_key = key;
		return true;
	}

	void PakReader::Close()
	{
		if (m_file.is_open()) m_file.close();
		m_file.clear();
		m_fileSize = 0;
		m_index.clear();
	}

	bool PakReader::Has(const std::string& virtualPath) const
	{
		return m_index.find(Hash64(virtualPath)) != m_index.end();
	}

	PakReader::ReadResult PakReader::TryRead(const std::string& virtualPath, std::vector<uint8_t>& outBytes)
	{
		auto it = m_index.find(Hash64(virtualPath));
		if (it == m_index.end() || !m_file.is_open()) return ReadResult::NotFound;

		const PakEntry& entry = it->second;
		// entry.dataSize は常に uint64_t だが、size_t は 32bit ビルド (Win32) では
		// 32bit しかない。ここでチェックせず static_cast<size_t> で黙って切り詰めると、
		// 確保する vector は小さいまま、直後の read() には切り詰め前の 64bit 値を
		// そのまま渡してしまい (streamsize は常に 64bit)、ヒープバッファオーバーフローに
		// なる。fileSize 以内であることは Open() 側で検証済みだが、32bit ビルドで
		// 4GB を超える実ファイルを扱うケース自体を明示的に弾く。
		if (entry.dataSize > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
		{
			return ReadResult::Failed;
		}
		std::vector<uint8_t> data(static_cast<size_t>(entry.dataSize));
		if (entry.dataSize > 0)
		{
			m_file.seekg(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
			m_file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(entry.dataSize));
			if (!m_file || static_cast<uint64_t>(m_file.gcount()) != entry.dataSize)
			{
				// 失敗した read() で立った failbit/eofbit をクリアしておかないと、
				// m_file は以降ずっと no-op になり (GlobalPakReader() はプロセス全体で
				// 使い回される単一インスタンスのため)、この1回の失敗で以後すべての
				// アセット読み込みが無言で失敗し続けてしまう。
				m_file.clear();
				return ReadResult::Failed;
			}

			if (entry.flags & kPakFlagEncrypted)
			{
				Chacha20Xor(m_key.data(), entry.nonce, 0, data.data(), data.size());
			}
		}

		outBytes = std::move(data);
		return ReadResult::Success;
	}

	std::vector<uint8_t> PakReader::Read(const std::string& virtualPath)
	{
		std::vector<uint8_t> data;
		TryRead(virtualPath, data);
		return data;
	}
}
