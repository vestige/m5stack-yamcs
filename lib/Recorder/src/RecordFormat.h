#pragma once

#include <stddef.h>
#include <stdint.h>

// データレコーダ (#55) の記録の決まりごと。SD カードには依存しない (テストできる部分)。
//
// 送るテレメトリのパケットを、どの機器のデータかで分けて記録する (ペイロードごとに保存する)。
//   /rec/<機器>/<年月日>/<時>.bin    例: /rec/gnss/20261030/06.bin
// 時刻は、パケットの副ヘッダの機上時刻 (UTC) で決める。機上時刻がわからないパケットは記録しない。
// ファイルの中は、1件ずつ「長さ (uint16、ビッグエンディアン) + パケットそのまま」を並べる。
namespace RecordFormat {

// どの機器のデータか。ペイロードが増えたら、ここに足す (#31、#47)
enum class Source : uint8_t {
  Bus = 0,   // 衛星バス: HK、ACK、イベント
  Gnss = 1,  // ペイロード (GNSS 受信機): GNSS のデータ、観測データ
};
const size_t kSourceCount = 2;

const size_t kLengthSize = 2;
const size_t kMaxPathLength = 32;

// パケットの APID から、どの機器のデータかを決める。記録しないパケットなら false
bool sourceOf(const uint8_t* packet, size_t size, Source* source);
const char* sourceName(Source source);

// パケットの副ヘッダの機上時刻 (Unix 時刻のミリ秒)。副ヘッダが無いか、時刻がわからなければ0
uint64_t packetTimeMs(const uint8_t* packet, size_t size);

// 記録するファイルの名前と、その機器のフォルダ (/rec/<機器>) を作る
void filePath(Source source, uint32_t utcSec, char* out, size_t size);
void sourceDir(Source source, char* out, size_t size);

// 1件の記録の頭 (長さ) を書き込む
void writeLength(size_t packetSize, uint8_t* out);

}  // namespace RecordFormat
