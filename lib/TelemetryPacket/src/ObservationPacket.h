#pragma once

#include <NmeaParser.h>
#include <stddef.h>
#include <stdint.h>

// ペイロードの観測データ (見えている衛星ごとの信号の強さ) を CCSDS Space Packet (TM) に詰める。
// MISSIONモードのときだけ送る。
//
// offset size  field
//      0    6  CCSDS primary header (APID=104)
//      6    4  uint32  UTC time (Unix秒。時刻が無効なら0)
//     10    1  uint8   number of satellites in view (この後の一覧の数, 最大 kMaxSatellites)
//     11    1  uint8   number of tracked satellites (C/N0 > 0)
//     12    1  uint8   max C/N0 (dB-Hz)
//     13    1  uint8   average C/N0 of tracked satellites (dB-Hz)
//     14    5  uint8   tracked satellites per system (GPS, GLONASS, Galileo, BeiDou, QZSS)
//     19  6*N         satellites (C/N0 の強い順)
//                      uint8 system (GnssSystem), uint8 PRN, int8 elevation (度, 不明は-1),
//                      uint16 azimuth (度, 不明は0xFFFF), uint8 C/N0 (dB-Hz)
namespace ObservationPacket {

const uint16_t kApid = 104;
const size_t kMaxSatellites = 32;
const size_t kHeaderSize = 19;
const size_t kEntrySize = 6;
const size_t kMaxPacketSize = kHeaderSize + kEntrySize * kMaxSatellites;

// 衛星の一覧 (信号の強い順に並んでいること) をパケットにして書き込み、バイト数を返す。
// kMaxSatellites を超える分は捨てる。buffer が小さければ0を返す
size_t encode(uint32_t utcTime, const SatelliteInfo* satellites, size_t count, uint16_t sequenceCount,
              uint8_t* buffer, size_t size);

}  // namespace ObservationPacket
