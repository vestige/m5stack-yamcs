#include "AckPacket.h"

#include "PacketWriter.h"

namespace AckPacket {

size_t encode(uint16_t tcSequenceCount, uint8_t commandId, Stage stage, uint8_t errorCode,
              uint16_t sequenceCount, uint8_t* buffer, size_t size) {
  if (size < kPacketSize) return 0;

  PacketWriter w(buffer);
  w.primaryHeader(kApid, sequenceCount, kPacketSize);
  w.u16(tcSequenceCount & 0x3FFF);
  w.u8(commandId);
  w.u8(static_cast<uint8_t>(stage));
  w.u8(errorCode);
  return kPacketSize;
}

}  // namespace AckPacket
