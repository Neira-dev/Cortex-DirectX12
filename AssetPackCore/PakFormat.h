#pragma once
#include <cstdint>

// Cortex アセットパック (.cpak) のバイナリレイアウト。
//
// [PakHeader]
// [PakEntry] * entryCount   (indexOffset から entryCount 個並ぶ)
// [暗号化された生データ] ...  (各エントリの dataOffset/dataSize が指す領域)
//
// 設計方針 (現状のスコープ):
//   - 中身は元の .fbx / .png 等のバイトを ChaCha20 で暗号化しただけで、独自バイナリ形式への
//     変換は行わない。よって「フォルダを開いても .fbx/.png がそのまま見える」状態と
//     「アーカイブを素朴に展開すれば中身が読める」状態の 2 つを防ぐのが目的であり、
//     鍵を含む実行ファイルを解析すれば復号自体は可能 (真の DRM ではなく妨害レベル)。
//   - 仮想パスは平文で保存せず 64bit ハッシュのみを保持する (PathHash.h)。
//     ハッシュ一覧から元のパス文字列を逆引きすることはできない。

namespace AssetPack
{
	constexpr char     kPakMagic[4] = { 'C','P','A','K' };
	constexpr uint32_t kPakVersion  = 1;

	// エントリ内データの種別ヒント (拡張子から機械的に決定。読み込み側が
	// 復号後のバイトをどう解釈すべきか判断する材料として使う)。
	// 4 文字 ASCII を uint32 にパックしたタグ (例: 'F','B','X',' ')。
	uint32_t MakeTypeTag(char a, char b, char c, char d);

	constexpr uint32_t kPakFlagEncrypted = 1u << 0;

#pragma pack(push, 1)
	struct PakHeader
	{
		char     magic[4];      // "CPAK"
		uint32_t version;       // kPakVersion
		uint32_t entryCount;
		uint32_t flags;         // 現状未使用 (将来の全体オプション用)
		uint64_t indexOffset;   // ファイル先頭からエントリ配列までのオフセット
		uint8_t  reserved[8];
	};
	static_assert(sizeof(PakHeader) == 32, "PakHeader size drifted");

	struct PakEntry
	{
		uint64_t pathHash;      // PathHash::Hash64(仮想パス)
		uint64_t dataOffset;    // ファイル先頭からデータ本体までのオフセット
		uint64_t dataSize;      // 暗号化後 (= 平文と同サイズ) のバイト数
		uint32_t typeTag;       // MakeTypeTag() の結果
		uint32_t flags;         // kPakFlagEncrypted など
		uint8_t  nonce[12];     // ChaCha20 nonce (エントリごとに一意)
		uint8_t  reserved[4];
	};
	static_assert(sizeof(PakEntry) == 48, "PakEntry size drifted");
#pragma pack(pop)
}
