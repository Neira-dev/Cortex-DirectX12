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

	bool PakWriter::WriteTo(const std::string& outPakPath, const Key32& key) const
	{
		// パスハッシュの重複チェック (異なる仮想パスが同じハッシュに落ちる事故を検出)
		{
			std::unordered_set<uint64_t> seen;
			for (const auto& e : m_entries)
			{
				const uint64_t h = Hash64(e.virtualPath);
				if (!seen.insert(h).second)
				{
					return false;
				}
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

		std::vector<PakEntry> indexEntries;
		indexEntries.reserve(m_entries.size());
		std::vector<std::vector<uint8_t>> encrypted;
		encrypted.reserve(m_entries.size());

		uint64_t runningOffset = dataStart;
		for (const auto& e : m_entries)
		{
			PakEntry pe{};
			pe.pathHash = Hash64(e.virtualPath);
			pe.dataOffset = runningOffset;
			pe.dataSize = e.bytes.size();
			pe.typeTag = e.typeTag;
			pe.flags = kPakFlagEncrypted;
			for (auto& b : pe.nonce) b = static_cast<uint8_t>(rd() & 0xFF);

			std::vector<uint8_t> cipher = e.bytes;
			Chacha20Xor(key.data(), pe.nonce, 0, cipher.data(), cipher.size());

			runningOffset += pe.dataSize;
			indexEntries.push_back(pe);
			encrypted.push_back(std::move(cipher));
		}

		out.write(reinterpret_cast<const char*>(&header), sizeof(header));
		for (const auto& pe : indexEntries)
		{
			out.write(reinterpret_cast<const char*>(&pe), sizeof(pe));
		}
		for (const auto& blob : encrypted)
		{
			if (!blob.empty())
			{
				out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
			}
		}

		return static_cast<bool>(out);
	}
}
