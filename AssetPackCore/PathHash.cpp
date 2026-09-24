#include "PathHash.h"

namespace AssetPack
{
	std::string NormalizePath(const std::string& virtualPath)
	{
		std::string out;
		out.reserve(virtualPath.size());
		for (char ch : virtualPath)
		{
			if (ch == '\\') ch = '/';
			out.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(ch))));
		}
		size_t start = 0;
		while (out.compare(start, 2, "./") == 0) start += 2;
		return out.substr(start);
	}

	uint64_t Hash64(const std::string& virtualPath)
	{
		const std::string normalized = NormalizePath(virtualPath);
		// FNV-1a 64bit
		uint64_t hash = 1469598103934665603ull;
		for (unsigned char ch : normalized)
		{
			hash ^= ch;
			hash *= 1099511628211ull;
		}
		return hash;
	}
}
