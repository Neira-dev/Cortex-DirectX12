#include "Chacha20.h"
#include <cstring>

namespace
{
	inline uint32_t RotL(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }

	inline void QuarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d)
	{
		a += b; d ^= a; d = RotL(d, 16);
		c += d; b ^= c; b = RotL(b, 12);
		a += b; d ^= a; d = RotL(d, 8);
		c += d; b ^= c; b = RotL(b, 7);
	}

	inline uint32_t LoadLE32(const uint8_t* p)
	{
		return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
			(static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
	}

	inline void StoreLE32(uint8_t* p, uint32_t v)
	{
		p[0] = static_cast<uint8_t>(v);
		p[1] = static_cast<uint8_t>(v >> 8);
		p[2] = static_cast<uint8_t>(v >> 16);
		p[3] = static_cast<uint8_t>(v >> 24);
	}

	// 1 ブロック (64 バイト) 分のキーストリームを生成する。
	void Chacha20Block(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t out[64])
	{
		static const uint32_t kConst[4] = { 0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u };

		uint32_t state[16];
		state[0] = kConst[0]; state[1] = kConst[1]; state[2] = kConst[2]; state[3] = kConst[3];
		for (int i = 0; i < 8; ++i) state[4 + i] = LoadLE32(key + i * 4);
		state[12] = counter;
		for (int i = 0; i < 3; ++i) state[13 + i] = LoadLE32(nonce + i * 4);

		uint32_t work[16];
		std::memcpy(work, state, sizeof(work));

		for (int round = 0; round < 10; ++round)
		{
			// column rounds
			QuarterRound(work[0], work[4], work[8], work[12]);
			QuarterRound(work[1], work[5], work[9], work[13]);
			QuarterRound(work[2], work[6], work[10], work[14]);
			QuarterRound(work[3], work[7], work[11], work[15]);
			// diagonal rounds
			QuarterRound(work[0], work[5], work[10], work[15]);
			QuarterRound(work[1], work[6], work[11], work[12]);
			QuarterRound(work[2], work[7], work[8], work[13]);
			QuarterRound(work[3], work[4], work[9], work[14]);
		}

		for (int i = 0; i < 16; ++i)
		{
			StoreLE32(out + i * 4, work[i] + state[i]);
		}
	}
}

namespace AssetPack
{
	void Chacha20Xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
	                  uint8_t* data, size_t len)
	{
		uint8_t block[64];
		size_t offset = 0;
		while (offset < len)
		{
			Chacha20Block(key, nonce, counter, block);
			++counter;
			const size_t chunk = (len - offset) < 64 ? (len - offset) : 64;
			for (size_t i = 0; i < chunk; ++i)
			{
				data[offset + i] ^= block[i];
			}
			offset += chunk;
		}
	}
}
