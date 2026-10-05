#include "TelemetryPacket.h"

#include <string.h>

namespace TelemetryPacket {

namespace {

class Writer {
 public:
  explicit Writer(uint8_t* buffer) : p_(buffer) {}

  void u8(uint8_t v) { *p_++ = v; }
  void u16(uint16_t v) {
    u8(static_cast<uint8_t>(v >> 8));
    u8(static_cast<uint8_t>(v));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v >> 16));
    u16(static_cast<uint16_t>(v));
  }
  void u64(uint64_t v) {
    u32(static_cast<uint32_t>(v >> 32));
    u32(static_cast<uint32_t>(v));
  }
  void f32(float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    u32(bits);
  }
  void f64(double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    u64(bits);
  }

 private:
  uint8_t* p_;
};

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

  Writer w(buffer);

  // Primary header
  // version(3)=0, type(1)=0:TM, secondary header flag(1)=0, APID(11)
  w.u16(kApid & 0x07FF);
  // sequence flags(2)=0b11:unsegmented, sequence count(14)
  w.u16(0xC000 | (sequenceCount & 0x3FFF));
  // packet data length = データ部のバイト数 - 1
  w.u16(static_cast<uint16_t>(kPacketSize - kPrimaryHeaderSize - 1));

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
