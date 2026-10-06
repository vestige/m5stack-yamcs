#include "HousekeepingPacket.h"

#include "PacketWriter.h"

namespace HousekeepingPacket {

size_t encode(const Housekeeping& hk, uint16_t sequenceCount, uint8_t* buffer, size_t size) {
  if (size < kPacketSize) return 0;

  PacketWriter w(buffer);
  w.primaryHeader(kApid, sequenceCount, kPacketSize);

  w.u8(static_cast<uint8_t>(hk.mode));
  w.u8(hk.payloadPower ? 1 : 0);
  w.u16(hk.acceptedCommands);
  w.u16(hk.rejectedCommands);
  w.u8(hk.lastCommandId);
  w.i8(hk.wifiRssi);
  w.u32(hk.freeHeap);
  w.u32(hk.minFreeHeap);
  w.u32(hk.uptimeMs);
  w.u16(hk.bootCount);
  w.u8(hk.resetReason);

  return kPacketSize;
}

}  // namespace HousekeepingPacket
