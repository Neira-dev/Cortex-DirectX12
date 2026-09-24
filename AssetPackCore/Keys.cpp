#include "Keys.h"
#include <fstream>
#include <random>

namespace AssetPack
{
	const Key32& DefaultKey()
	{
		// 適当な固定バイト列 (乱数的な見た目にしているだけで暗号学的な強度の根拠はない)。
		static const Key32 key = { {
			0x4a,0x1e,0x9c,0x77,0x2b,0xf0,0x63,0xd5,
			0x8e,0x11,0xa4,0x5c,0x99,0x37,0x02,0xbb,
			0xc6,0x84,0x1f,0x2a,0x7d,0xe9,0x50,0x3c,
			0xab,0x16,0x6e,0xf4,0x0d,0x88,0x2f,0x91
		} };
		return key;
	}

	Key32 GenerateRandomKey()
	{
		Key32 key{};
		std::random_device rd;
		for (auto& b : key)
		{
			b = static_cast<uint8_t>(rd() & 0xFF);
		}
		return key;
	}

	bool LoadKeyFile(const std::string& path, Key32& outKey)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return false;
		f.read(reinterpret_cast<char*>(outKey.data()), static_cast<std::streamsize>(outKey.size()));
		return f.gcount() == static_cast<std::streamsize>(outKey.size());
	}

	bool SaveKeyFile(const std::string& path, const Key32& key)
	{
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f) return false;
		f.write(reinterpret_cast<const char*>(key.data()), static_cast<std::streamsize>(key.size()));
		return static_cast<bool>(f);
	}
}
