#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ObservationPacket.h"
#include "PacketWriter.h"

// データレコーダから降ろす (ダンプする) パケット (#56)。記録したパケットを、そのまま中に入れて送る。
// 地上局はこの APID でダンプと見分け、中のパケットを取り出して YAMCS のダンプ用のリンクへ送る。
// 本物の衛星では、ダンプとリアルタイムを仮想チャネル (Virtual Channel) で分けることが多い。
// M5Sat には伝送フレームの層が無いので、外側のパケットで分ける。
//
// offset size  field
//      0    6  CCSDS primary header (secondary header flag=1) (APID=105)
//      6    8  uint64  packet time (ダンプで送った機上時刻、Unix 時刻のミリ秒)
//     14    -  記録したパケット (自分の副ヘッダに、記録した時刻を持っている)
namespace DumpPacket {

const uint16_t kApid = 105;
// 中に入れるパケットでいちばん大きいのは観測データ
const size_t kMaxRecordedSize = ObservationPacket::kMaxPacketSize;
const size_t kMaxPacketSize = PacketWriter::kHeaderSize + kMaxRecordedSize;

// recorded を中に入れたパケットを書き込み、バイト数を返す。recorded が大きすぎるか、buffer が小さければ0
size_t encode(const uint8_t* recorded, size_t recordedSize, uint16_t sequenceCount, uint64_t timeMs, uint8_t* buffer,
              size_t size);

}  // namespace DumpPacket
