// AssetPacker
//
// 開発中のルーズファイル (Assets/ 配下の .fbx/.png 等) を集めて、暗号化した
// 単一の .cpak アーカイブへ書き出すビルド時ツール。
//
// 使い方:
//   AssetPacker.exe --input Assets --output Assets.cpak [--key-file pak.key] [--verify]
//
// 想定運用: 開発中の反復作業 (モデル差し替え等) はエンジン側がルーズファイルを
// 直接読む前提のため、このツールは「配布ビルドを作る直前」に 1 回走らせるだけでよい。
// 毎回の実行コストを気にする必要はない (実行のたびに pak 化する運用は想定していない)。
#include "../AssetPackCore/PakWriter.h"
#include "../AssetPackCore/PakReader.h"
#include "../AssetPackCore/PakFormat.h"
#include "../AssetPackCore/Keys.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;
using namespace AssetPack;

namespace
{
	struct Options
	{
		std::string input = "Assets";
		std::string output = "Assets.cpak";
		std::string keyFile;
		bool verify = false;
	};

	bool ParseArgs(int argc, char** argv, Options& opt)
	{
		for (int i = 1; i < argc; ++i)
		{
			const std::string arg = argv[i];
			auto next = [&](std::string& dst) -> bool
			{
				if (i + 1 >= argc) return false;
				dst = argv[++i];
				return true;
			};
			if (arg == "--input") { if (!next(opt.input)) return false; }
			else if (arg == "--output") { if (!next(opt.output)) return false; }
			else if (arg == "--key-file") { if (!next(opt.keyFile)) return false; }
			else if (arg == "--verify") { opt.verify = true; }
			else if (arg == "--help" || arg == "-h")
			{
				std::cout <<
					"Usage: AssetPacker --input <dir> --output <file.cpak> [--key-file <key32.bin>] [--verify]\n"
					"  --input      走査するルートフォルダ (既定: Assets)\n"
					"  --output     出力 .cpak パス (既定: Assets.cpak)\n"
					"  --key-file   32byte 生鍵ファイル。省略時は AssetPack::DefaultKey() を使用\n"
					"               (本番配布では必ず自前の鍵を指定すること)\n"
					"  --verify     書き出し直後に全エントリを読み戻してバイト一致を確認する\n";
				return false;
			}
			else
			{
				std::cerr << "unknown option: " << arg << "\n";
				return false;
			}
		}
		return true;
	}

	uint32_t TypeTagForExtension(const std::string& ext)
	{
		if (ext == ".fbx")  return MakeTypeTag('F', 'B', 'X', ' ');
		if (ext == ".obj")  return MakeTypeTag('O', 'B', 'J', ' ');
		if (ext == ".gltf") return MakeTypeTag('G', 'L', 'T', 'F');
		if (ext == ".glb")  return MakeTypeTag('G', 'L', 'B', ' ');
		if (ext == ".png")  return MakeTypeTag('P', 'N', 'G', ' ');
		if (ext == ".jpg" || ext == ".jpeg") return MakeTypeTag('J', 'P', 'G', ' ');
		if (ext == ".tga")  return MakeTypeTag('T', 'G', 'A', ' ');
		if (ext == ".hdr")  return MakeTypeTag('H', 'D', 'R', ' ');
		if (ext == ".dds")  return MakeTypeTag('D', 'D', 'S', ' ');
		return MakeTypeTag('B', 'I', 'N', ' ');
	}

	// 既定で対象にする拡張子。Assets/UI (.rml/.rcss) 等はここに含めていないので
	// 従来通りルーズファイルのまま配布される (対象を広げたい場合はここへ追記する)。
	bool IsTrackedExtension(const std::string& ext)
	{
		static const std::vector<std::string> kExts = {
			".fbx", ".obj", ".gltf", ".glb", ".png", ".jpg", ".jpeg", ".tga", ".hdr", ".dds"
		};
		return std::find(kExts.begin(), kExts.end(), ext) != kExts.end();
	}

	std::string ToVirtualPath(const fs::path& p)
	{
		std::string s = p.generic_string();
		return s;
	}

	bool ReadFileBytes(const fs::path& p, std::vector<uint8_t>& out)
	{
		std::ifstream f(p, std::ios::binary | std::ios::ate);
		if (!f) return false;
		const auto size = f.tellg();
		if (size < 0) return false;
		out.resize(static_cast<size_t>(size));
		f.seekg(0);
		if (!out.empty())
		{
			f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
		}
		return static_cast<bool>(f) || f.eof();
	}
}

int main(int argc, char** argv)
{
	Options opt;
	if (!ParseArgs(argc, argv, opt))
	{
		return 1;
	}

	if (!fs::exists(opt.input) || !fs::is_directory(opt.input))
	{
		std::cerr << "input directory not found: " << opt.input << "\n";
		return 1;
	}

	Key32 key = DefaultKey();
	if (!opt.keyFile.empty())
	{
		if (!LoadKeyFile(opt.keyFile, key))
		{
			std::cerr << "failed to read key file (expects 32 raw bytes): " << opt.keyFile << "\n";
			return 1;
		}
		std::cout << "using key file: " << opt.keyFile << "\n";
	}
	else
	{
		std::cout << "warning: no --key-file given, using AssetPackCore's built-in DefaultKey().\n"
			"         replace this before shipping a real build (see Keys.h).\n";
	}

	PakWriter writer;
	std::vector<std::pair<std::string, std::vector<uint8_t>>> originals; // --verify 用

	size_t scanned = 0;
	for (const auto& entry : fs::recursive_directory_iterator(opt.input))
	{
		if (!entry.is_regular_file()) continue;
		const std::string ext = entry.path().extension().generic_string();
		std::string extLower = ext;
		std::transform(extLower.begin(), extLower.end(), extLower.begin(),
			[](unsigned char c) { return static_cast<char>(::tolower(c)); });
		if (!IsTrackedExtension(extLower)) continue;

		std::vector<uint8_t> bytes;
		if (!ReadFileBytes(entry.path(), bytes))
		{
			std::cerr << "failed to read: " << entry.path().string() << "\n";
			return 1;
		}

		const std::string virtualPath = ToVirtualPath(entry.path());
		writer.AddFile(virtualPath, bytes, TypeTagForExtension(extLower));
		if (opt.verify) originals.emplace_back(virtualPath, bytes);

		std::cout << "  + " << virtualPath << " (" << bytes.size() << " bytes)\n";
		++scanned;
	}

	if (scanned == 0)
	{
		std::cout << "no matching files found under " << opt.input << " (nothing to pack).\n";
		return 0;
	}

	if (!writer.WriteTo(opt.output, key))
	{
		std::cerr << "failed to write " << opt.output << "\n";
		return 1;
	}

	std::cout << "packed " << scanned << " file(s) -> " << opt.output << "\n";

	if (opt.verify)
	{
		PakReader reader;
		if (!reader.Open(opt.output, key))
		{
			std::cerr << "verify: failed to reopen " << opt.output << "\n";
			return 1;
		}
		size_t ok = 0;
		for (const auto& [virtualPath, bytes] : originals)
		{
			std::vector<uint8_t> readBack = reader.Read(virtualPath);
			if (readBack != bytes)
			{
				std::cerr << "verify: MISMATCH for " << virtualPath << "\n";
				return 1;
			}
			++ok;
		}
		std::cout << "verify: " << ok << "/" << originals.size() << " entries round-tripped correctly.\n";
	}

	return 0;
}
