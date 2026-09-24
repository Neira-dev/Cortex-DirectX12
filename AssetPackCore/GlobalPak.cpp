#include "GlobalPak.h"

namespace AssetPack
{
	GlobalPak* GlobalPak::instance = nullptr;

	GlobalPak* GlobalPak::Get()
	{
		if (instance == nullptr)
		{
			instance = new GlobalPak();
		}
		return instance;
	}

	void GlobalPak::Del()
	{
		if (instance != nullptr)
		{
			delete instance;
			instance = nullptr;
		}
	}

	bool GlobalPak::TryOpen(const std::string& pakPath, const Key32& key)
	{
		return m_reader.Open(pakPath, key);
	}
}
