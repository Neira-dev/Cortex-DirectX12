#pragma once
#include <cstdint>
#include <cstddef>

// 自己完結の ChaCha20 (RFC 8439 準拠、外部ライブラリ非依存)。
// 目的はアセットの「そのまま流用」を妨害することであり、鍵は実行ファイル内に
// 埋め込まれる前提のため暗号学的な意味での機密性は保証しない (Keys.h のコメント参照)。
namespace AssetPack
{
	// key: 32 bytes, nonce: 12 bytes, counter: ブロックカウンタ初期値 (通常 0)。
	// data を同じ鍵ストリームで XOR する (暗号化・復号どちらも同じ関数)。
	void Chacha20Xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
	                  uint8_t* data, size_t len);
}
