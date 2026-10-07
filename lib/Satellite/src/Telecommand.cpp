#include "Telecommand.h"

namespace Telecommand {

ErrorCode parse(const uint8_t* data, size_t size, Packet* out, bool* hasHeader) {
  *hasHeader = false;
  if (size < kPrimaryHeaderSize) return ErrorCode::MalformedPacket;

  uint16_t word1 = static_cast<uint16_t>((data[0] << 8) | data[1]);
  uint16_t word2 = static_cast<uint16_t>((data[2] << 8) | data[3]);
  uint16_t length = static_cast<uint16_t>((data[4] << 8) | data[5]);

  out->sequenceCount = word2 & 0x3FFF;
  *hasHeader = true;

  uint8_t version = word1 >> 13;
  bool isTelecommand = (word1 >> 12) & 1;
  uint16_t apid = word1 & 0x07FF;
  if (version != 0 || !isTelecommand || apid != kApid) return ErrorCode::MalformedPacket;
  // packet data length = データ部のバイト数 - 1
  if (static_cast<size_t>(length) + 1 + kPrimaryHeaderSize != size) return ErrorCode::MalformedPacket;
  if (size < kPrimaryHeaderSize + 1) return ErrorCode::MalformedPacket;

  out->commandId = data[kPrimaryHeaderSize];
  out->args = data + kPrimaryHeaderSize + 1;
  out->argsLength = size - kPrimaryHeaderSize - 1;
  return ErrorCode::None;
}

}  // namespace Telecommand
