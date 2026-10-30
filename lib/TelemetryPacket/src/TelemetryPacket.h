#pragma once

#include <NmeaParser.h>
#include <stddef.h>
#include <stdint.h>

// GNSSテレメトリを CCSDS Space Packet (TM) に詰める。
// 数値はすべてビッグエンディアン。
//
// offset size  field
//      0    6  CCSDS primary header (secondary header flag=1) (version=0, type=TM, APID, sequence count, length)
//      6    8  uint64  packet time (機上時刻、Unix 時刻のミリ秒。わからなければ0)
//     14    4  uint32  UTC time (Unix秒。時刻が無効なら0)
//     18    2  uint16  UTC millisecond
//     20    8  float64 latitude (deg, 北緯が正)
//     28    8  float64 longitude (deg, 東経が正)
//     36    4  float32 altitude (m, 海抜)
//     40    1  uint8   satellites (測位に使用している衛星数)
//     41    1  uint8   fix quality (GGA)
//     42    1  uint8   fix type (GSA: 1=no fix, 2=2D, 3=3D)
//     43    1  uint8   flags (TelemetryFlag)
//     44    4  uint32  uptime (ms)
//     48    4  uint32  valid NMEA sentences
//     52    4  uint32  NMEA checksum errors
namespace TelemetryPacket {

const uint16_t kApid = 100;
const size_t kPrimaryHeaderSize = 6;
const size_t kPacketSize = 56;

enum TelemetryFlag : uint8_t {
  kTimeValid = 1 << 0,
  kDateValid = 1 << 1,
  kPositionValid = 1 << 2,
  kAltitudeValid = 1 << 3,
  kRmcActive = 1 << 4,
};

// パケットをbufferに書き込み、書き込んだバイト数を返す。bufferが小さければ0を返す。
// sequenceCount は下位14bitが使われる。
size_t encode(const GnssTelemetry& t, uint32_t validSentences, uint32_t checksumErrors,
              uint32_t uptimeMs, uint16_t sequenceCount, uint64_t timeMs, uint8_t* buffer, size_t size);

// UTCの日付・時刻をUnix秒に変換する
uint32_t toUnixTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                    uint8_t second);

}  // namespace TelemetryPacket
