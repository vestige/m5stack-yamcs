#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// パケットにビッグエンディアンで値を書き込む
class PacketWriter {
 public:
  explicit PacketWriter(uint8_t* buffer) : p_(buffer) {}

  void u8(uint8_t v) { *p_++ = v; }
  void i8(int8_t v) { u8(static_cast<uint8_t>(v)); }
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

  // CCSDS Primary Header (TM, 2次ヘッダなし, 分割なし) を書き込む
  void primaryHeader(uint16_t apid, uint16_t sequenceCount, size_t packetSize) {
    // version(3)=0, type(1)=0:TM, secondary header flag(1)=0, APID(11)
    u16(apid & 0x07FF);
    // sequence flags(2)=0b11:unsegmented, sequence count(14)
    u16(0xC000 | (sequenceCount & 0x3FFF));
    // packet data length = データ部のバイト数 - 1
    u16(static_cast<uint16_t>(packetSize - kPrimaryHeaderSize - 1));
  }

  static const size_t kPrimaryHeaderSize = 6;

 private:
  uint8_t* p_;
};
