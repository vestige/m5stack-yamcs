#pragma once

#include <stddef.h>
#include <stdint.h>

// シリアル (バイトの流れ) でパケットを送るときの区切り方。
//
//   ASM (1A CF FC 1D) | length (uint16, big endian) | payload | CRC-16-CCITT (uint16)
//
// ASM (Attached Sync Marker) はCCSDSの通信でもパケットの始まりの印として使われる値。
// CRCは length と payload にかける。
namespace SerialFrame {

const uint8_t kSyncMarker[4] = {0x1A, 0xCF, 0xFC, 0x1D};
const size_t kMaxPayload = 256;
const size_t kOverhead = 4 + 2 + 2;

// CRC-16-CCITT (初期値 0xFFFF, 多項式 0x1021)
uint16_t crc16(const uint8_t* data, size_t size, uint16_t crc = 0xFFFF);

// payload をフレームにして out に書き込み、書き込んだバイト数を返す。収まらなければ0
size_t encode(const uint8_t* payload, size_t size, uint8_t* out, size_t capacity);

// 1バイトずつ入力してフレームを取り出す
class Decoder {
 public:
  // フレームを1つ取り出し終えたら true。payload() / size() で中身を読める
  bool push(uint8_t byte);

  const uint8_t* payload() const { return payload_; }
  size_t size() const { return size_; }
  uint32_t crcErrors() const { return crcErrors_; }

 private:
  enum class State { Sync, LengthHigh, LengthLow, Payload, CrcHigh, CrcLow };

  void reset();

  State state_ = State::Sync;
  size_t syncMatched_ = 0;
  size_t size_ = 0;
  size_t received_ = 0;
  uint16_t crc_ = 0;
  uint8_t payload_[kMaxPayload];
  uint32_t crcErrors_ = 0;
};

}  // namespace SerialFrame
