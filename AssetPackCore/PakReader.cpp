#include "PakReader.h"
#include "PathHash.h"
#include "Chacha20.h"
#include <cstring>

namespace AssetPack
{
	bool PakReader::Open(const std::string& pakPath, const Key32& key)
	{
		Close();

		m_file.open(pakPath, std::ios::binary);
		if (!m_file) return false;

		PakHeader header{};
		m_file.read(reinterpret_cast<char*>(&header), sizeof(header));
		if (!m_file || m_file.gcount() != sizeof(header))
		{
			Close();
			return false;
		}
		if (std::memcmp(header.magic, kPakMagic, 4) != 0 || header.version != kPakVersion)
		{
			Close();
			return false;
		}

		m_file.seekg(static_cast<std::streamoff>(header.indexOffset), std::ios::beg);
		m_index.reserve(header.entryCount);
		for (uint32_t i = 0; i < header.entryCount; ++i)
		{
			PakEntry entry{};
			m_file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
			if (!m_file || m_file.gcount() != sizeof(entry))
			{
				Close();
				return false;
			}
			m_index.emplace(entry.pathHash, entry);
		}

		m_key = key;
		return true;
	}

	void PakReader::Close()
	{
		if (m_file.is_open()) m_file.close();
		m_file.clear();
		m_index.clear();
	}

	bool PakReader::Has(const std::string& virtualPath) const
	{
		return m_index.find(Hash64(virtualPath)) != m_index.end();
	}

	std::vector<uint8_t> PakReader::Read(const std::string& virtualPath)
	{
		auto it = m_index.find(Hash64(virtualPath));
		if (it == m_index.end() || !m_file.is_open()) return {};

		const PakEntry& entry = it->second;
		std::vector<uint8_t> data(static_cast<size_t>(entry.dataSize));
		if (entry.dataSize == 0) return data;

		m_file.seekg(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
		m_file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(entry.dataSize));
		if (!m_file || static_cast<uint64_t>(m_file.gcount()) != entry.dataSize)
		{
			return {};
		}

		if (entry.flags & kPakFlagEncrypted)
		{
			Chacha20Xor(m_key.data(), entry.nonce, 0, data.data(), data.size());
		}
		return data;
	}
}
