#include "GlobalPak.h"

namespace AssetPack
{
	namespace
	{
		PakReader& Instance()
		{
			static PakReader reader;
			return reader;
		}
	}

	bool TryOpenGlobalPak(const std::string& pakPath, const Key32& key)
	{
		return Instance().Open(pakPath, key);
	}

	bool IsGlobalPakOpen()
	{
		return Instance().IsOpen();
	}

	PakReader& GlobalPakReader()
	{
		return Instance();
	}
}
