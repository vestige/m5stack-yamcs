#include "HousekeepingPacket.h"

#include "PacketWriter.h"

namespace HousekeepingPacket {

size_t encode(const Housekeeping& hk, uint16_t sequenceCount, uint64_t timeMs, uint8_t* buffer, size_t size) {
  if (size < kPacketSize) return 0;

  PacketWriter w(buffer);
  w.header(kApid, sequenceCount, kPacketSize, timeMs);

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
  w.u8(hk.fdirEnabled ? 1 : 0);
  w.u8(static_cast<uint8_t>(hk.lastFault));
  w.u16(hk.faultCount);
  w.u32(hk.onboardTime);
  w.u8(static_cast<uint8_t>(hk.observationState));
  w.u32(hk.observationStart);
  w.u16(hk.observationDuration);
  w.u8(static_cast<uint8_t>(hk.lastObservationResult));
  w.u32(hk.lastObservationStart);
  w.u16(hk.lastObservationLength);
  w.u16(hk.completedObservations);
  w.u8(static_cast<uint8_t>(hk.lampColor));
  w.u16(hk.lampRemaining);
  w.u16(hk.payloadOnTime);

  return kPacketSize;
}

}  // namespace HousekeepingPacket
