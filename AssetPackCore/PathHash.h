#pragma once
#include <cstdint>
#include <string>

namespace AssetPack
{
	// 仮想パスを正規化 (小文字化 + バックスラッシュ→スラッシュ + 先頭の "./" 除去) した上で
	// FNV-1a 64bit ハッシュを返す。PakReader::Read / PakWriter::AddFile は必ずこれを通す。
	std::string NormalizePath(const std::string& virtualPath);
	uint64_t Hash64(const std::string& virtualPath);
}
