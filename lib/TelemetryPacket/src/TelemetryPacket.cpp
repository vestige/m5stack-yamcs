#include "TelemetryPacket.h"

#include "PacketWriter.h"

namespace TelemetryPacket {

namespace {

// 1970-01-01からの日数（グレゴリオ暦）
int32_t daysFromCivil(int32_t y, uint32_t m, uint32_t d) {
  y -= m <= 2;
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

}  // namespace

uint32_t toUnixTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
                    uint8_t second) {
  int32_t days = daysFromCivil(year, month, day);
  return static_cast<uint32_t>(days) * 86400u + hour * 3600u + minute * 60u + second;
}

size_t encode(const GnssTelemetry& t, uint32_t validSentences, uint32_t checksumErrors,
              uint32_t uptimeMs, uint16_t sequenceCount, uint8_t* buffer, size_t size) {
  if (size < kPacketSize) return 0;

  PacketWriter w(buffer);
  w.primaryHeader(kApid, sequenceCount, kPacketSize);

  // UTC時刻は日付と時刻の両方が揃ったときだけ入れる
  bool hasUtc = t.timeValid && t.dateValid;
  w.u32(hasUtc ? toUnixTime(t.year, t.month, t.day, t.hour, t.minute, t.second) : 0);
  w.u16(hasUtc ? t.millisecond : 0);

  w.f64(t.positionValid ? t.latitude : 0.0);
  w.f64(t.positionValid ? t.longitude : 0.0);
  w.f32(t.altitudeValid ? t.altitude : 0.0f);

  uint8_t flags = 0;
  if (t.timeValid) flags |= kTimeValid;
  if (t.dateValid) flags |= kDateValid;
  if (t.positionValid) flags |= kPositionValid;
  if (t.altitudeValid) flags |= kAltitudeValid;
  if (t.rmcActive) flags |= kRmcActive;

  w.u8(t.satellites);
  w.u8(t.fixQuality);
  w.u8(t.fixType);
  w.u8(flags);

  w.u32(uptimeMs);
  w.u32(validSentences);
  w.u32(checksumErrors);

  return kPacketSize;
}

}  // namespace TelemetryPacket
