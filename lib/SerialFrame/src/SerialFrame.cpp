#include "SerialFrame.h"

#include <string.h>

namespace SerialFrame {

uint16_t crc16(const uint8_t* data, size_t size, uint16_t crc) {
  for (size_t i = 0; i < size; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

size_t encode(const uint8_t* payload, size_t size, uint8_t* out, size_t capacity) {
  if (size > kMaxPayload || capacity < size + kOverhead) return 0;

  uint8_t* p = out;
  memcpy(p, kSyncMarker, sizeof(kSyncMarker));
  p += sizeof(kSyncMarker);
  *p++ = static_cast<uint8_t>(size >> 8);
  *p++ = static_cast<uint8_t>(size);
  memcpy(p, payload, size);
  p += size;

  uint16_t crc = crc16(out + sizeof(kSyncMarker), 2 + size);
  *p++ = static_cast<uint8_t>(crc >> 8);
  *p++ = static_cast<uint8_t>(crc);
  return static_cast<size_t>(p - out);
}

void Decoder::reset() {
  state_ = State::Sync;
  syncMatched_ = 0;
}

bool Decoder::push(uint8_t byte) {
  switch (state_) {
    case State::Sync:
      if (byte == kSyncMarker[syncMatched_]) {
        if (++syncMatched_ == sizeof(kSyncMarker)) state_ = State::LengthHigh;
      } else {
        // 途中まで一致していたら、このバイトが新しい印の先頭かもしれない
        syncMatched_ = byte == kSyncMarker[0] ? 1 : 0;
      }
      return false;

    case State::LengthHigh:
      size_ = static_cast<size_t>(byte) << 8;
      state_ = State::LengthLow;
      return false;

    case State::LengthLow:
      size_ |= byte;
      if (size_ > kMaxPayload) {
        reset();
        return false;
      }
      received_ = 0;
      state_ = size_ == 0 ? State::CrcHigh : State::Payload;
      return false;

    case State::Payload:
      payload_[received_++] = byte;
      if (received_ == size_) state_ = State::CrcHigh;
      return false;

    case State::CrcHigh:
      crc_ = static_cast<uint16_t>(byte) << 8;
      state_ = State::CrcLow;
      return false;

    case State::CrcLow: {
      crc_ |= byte;
      uint8_t length[2] = {static_cast<uint8_t>(size_ >> 8), static_cast<uint8_t>(size_)};
      uint16_t expected = crc16(payload_, size_, crc16(length, sizeof(length)));
      reset();
      if (crc_ != expected) {
        crcErrors_++;
        return false;
      }
      return true;
    }
  }
  return false;
}

}  // namespace SerialFrame
