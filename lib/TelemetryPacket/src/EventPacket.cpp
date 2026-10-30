#include "EventPacket.h"

#include "PacketWriter.h"

namespace EventPacket {

size_t encode(const Event& event, uint16_t sequenceCount, uint64_t timeMs, uint8_t* buffer, size_t size) {
  if (size < kPacketSize) return 0;

  PacketWriter w(buffer);
  w.header(kApid, sequenceCount, kPacketSize, timeMs);
  w.u8(static_cast<uint8_t>(event.severity));
  w.u8(static_cast<uint8_t>(event.id));
  w.u16(event.arg1);
  w.u16(event.arg2);
  return kPacketSize;
}

}  // namespace EventPacket
