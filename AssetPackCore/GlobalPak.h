#pragma once
#include <string>
#include "PakReader.h"
#include "Keys.h"

namespace AssetPack
{
	// プロセス全体で共有する単一の PakReader を、Mesh/Texture/Shader と同じ
	// static Get()/Del() シングルトンの流儀で管理するラッパー。
	//
	// 使い方 (Template.cpp、起動時に一度だけ):
	//   AssetPack::GlobalPak::Get()->TryOpen("Assets.cpak", key);
	// 使い方 (Mesh.cpp/Texture.cpp、読み込みのたびに):
	//   AssetPack::GlobalPak* pak = AssetPack::GlobalPak::Get();
	//   if (pak->IsOpen()) { auto result = pak->Reader().TryRead(path, bytes); ... }
	//
	// TryOpen() を一度も呼ばなければ IsOpen() は常に false のままで、呼び出し側は
	// 今まで通りルーズファイル読み込みにフォールバックし続ける (安全に常時呼べる設計)。
	class GlobalPak
	{
	public:
		static GlobalPak* Get();
		static void Del();

		bool TryOpen(const std::string& pakPath, const Key32& key = DefaultKey());
		bool IsOpen() const { return m_reader.IsOpen(); }
		PakReader& Reader() { return m_reader; }

	private:
		PakReader m_reader;
		static GlobalPak* instance;
	};
}
