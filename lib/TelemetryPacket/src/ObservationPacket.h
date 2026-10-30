#pragma once

#include <NmeaParser.h>
#include <stddef.h>
#include <stdint.h>

// ペイロードの観測データ (見えている衛星ごとの信号の強さ) を CCSDS Space Packet (TM) に詰める。
// MISSIONモードのときだけ送る。
//
// offset size  field
//      0    6  CCSDS primary header (secondary header flag=1) (APID=104)
//      6    8  uint64  packet time (機上時刻、Unix 時刻のミリ秒。わからなければ0)
//     14    4  uint32  UTC time (Unix秒。時刻が無効なら0)
//     18    1  uint8   number of satellites in view (この後の一覧の数, 最大 kMaxSatellites)
//     19    1  uint8   number of tracked satellites (C/N0 > 0)
//     20    1  uint8   max C/N0 (dB-Hz)
//     21    1  uint8   average C/N0 of tracked satellites (dB-Hz)
//     22    5  uint8   tracked satellites per system (GPS, GLONASS, Galileo, BeiDou, QZSS)
//     27  6*N  satellites (C/N0 の強い順)
//                      uint8 system (GnssSystem), uint8 PRN, int8 elevation (度, 不明は-1),
//                      uint16 azimuth (度, 不明は0xFFFF), uint8 C/N0 (dB-Hz)
namespace ObservationPacket {

const uint16_t kApid = 104;
const size_t kMaxSatellites = 32;
const size_t kHeaderSize = 27;
const size_t kEntrySize = 6;
const size_t kMaxPacketSize = kHeaderSize + kEntrySize * kMaxSatellites;

// 衛星の一覧 (信号の強い順に並んでいること) をパケットにして書き込み、バイト数を返す。
// kMaxSatellites を超える分は捨てる。buffer が小さければ0を返す
size_t encode(uint32_t utcTime, const SatelliteInfo* satellites, size_t count, uint16_t sequenceCount, uint64_t timeMs,
              uint8_t* buffer, size_t size);

}  // namespace ObservationPacket
