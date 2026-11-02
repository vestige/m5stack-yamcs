#include "RecordFormat.h"

#include <stdio.h>
#include <time.h>

namespace RecordFormat {

namespace {

const size_t kPrimaryHeaderSize = 6;
const size_t kSecondaryHeaderSize = 8;

}  // namespace

bool sourceOf(const uint8_t* packet, size_t size, Source* source) {
  if (size < kPrimaryHeaderSize) return false;
  uint16_t word1 = static_cast<uint16_t>((packet[0] << 8) | packet[1]);
  bool isTelecommand = (word1 >> 12) & 1;
  if (isTelecommand) return false;
  switch (word1 & 0x07FF) {
    case 101:  // HK
    case 102:  // ACK
    case 103:  // イベント
      *source = Source::Bus;
      return true;
    case 100:  // GNSS
    case 104:  // 観測データ
      *source = Source::Gnss;
      return true;
  }
  return false;
}

const char* sourceName(Source source) {
  switch (source) {
    case Source::Bus: return "bus";
    case Source::Gnss: return "gnss";
  }
  return "other";
}

uint64_t packetTimeMs(const uint8_t* packet, size_t size) {
  if (size < kPrimaryHeaderSize + kSecondaryHeaderSize) return 0;
  bool hasSecondaryHeader = (packet[0] >> 3) & 1;
  if (!hasSecondaryHeader) return 0;
  uint64_t t = 0;
  for (size_t i = 0; i < kSecondaryHeaderSize; i++) t = (t << 8) | packet[kPrimaryHeaderSize + i];
  return t;
}

void filePath(Source source, uint32_t utcSec, char* out, size_t size) {
  time_t t = static_cast<time_t>(utcSec);
  struct tm tm;
  gmtime_r(&t, &tm);
  snprintf(out, size, "/rec/%s/%04d%02d%02d/%02d.bin", sourceName(source), tm.tm_year + 1900, tm.tm_mon + 1,
           tm.tm_mday, tm.tm_hour);
}

void sourceDir(Source source, char* out, size_t size) { snprintf(out, size, "/rec/%s", sourceName(source)); }

void writeLength(size_t packetSize, uint8_t* out) {
  out[0] = static_cast<uint8_t>(packetSize >> 8);
  out[1] = static_cast<uint8_t>(packetSize);
}

}  // namespace RecordFormat
