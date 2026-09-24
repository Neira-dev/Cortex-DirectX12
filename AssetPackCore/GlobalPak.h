#pragma once
#include <string>
#include "PakReader.h"
#include "Keys.h"

namespace AssetPack
{
	// プロセス全体で共有する単一の PakReader。
	// エンジン起動時に一度だけ TryOpenGlobalPak() を呼ぶことを想定している
	// (呼ばなければ IsGlobalPakOpen() は常に false のままで、呼び出し側は
	//  今まで通りルーズファイル読み込みにフォールバックし続ける)。
	bool TryOpenGlobalPak(const std::string& pakPath, const Key32& key = DefaultKey());
	bool IsGlobalPakOpen();
	PakReader& GlobalPakReader();
}
