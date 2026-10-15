#include "ObservationPacket.h"

#include "PacketWriter.h"

namespace ObservationPacket {

namespace {

// まとめに出す衛星システムの並び
const GnssSystem kSummarySystems[] = {GnssSystem::GPS, GnssSystem::GLONASS, GnssSystem::Galileo,
                                      GnssSystem::BeiDou, GnssSystem::QZSS};

}  // namespace

size_t encode(uint32_t utcTime, const SatelliteInfo* satellites, size_t count, uint16_t sequenceCount,
              uint8_t* buffer, size_t size) {
  if (count > kMaxSatellites) count = kMaxSatellites;
  size_t packetSize = kHeaderSize + kEntrySize * count;
  if (size < packetSize) return 0;

  uint8_t tracked = 0;
  uint8_t maxCn0 = 0;
  uint32_t sumCn0 = 0;
  uint8_t perSystem[5] = {};
  for (size_t i = 0; i < count; i++) {
    const SatelliteInfo& sat = satellites[i];
    if (sat.snr == 0) continue;
    tracked++;
    sumCn0 += sat.snr;
    if (sat.snr > maxCn0) maxCn0 = sat.snr;
    for (size_t s = 0; s < 5; s++) {
      if (sat.system == kSummarySystems[s]) perSystem[s]++;
    }
  }

  PacketWriter w(buffer);
  w.primaryHeader(kApid, sequenceCount, packetSize);
  w.u32(utcTime);
  w.u8(static_cast<uint8_t>(count));
  w.u8(tracked);
  w.u8(maxCn0);
  w.u8(tracked ? static_cast<uint8_t>((sumCn0 + tracked / 2) / tracked) : 0);
  for (uint8_t n : perSystem) w.u8(n);

  for (size_t i = 0; i < count; i++) {
    const SatelliteInfo& sat = satellites[i];
    w.u8(static_cast<uint8_t>(sat.system));
    w.u8(sat.prn);
    w.i8(sat.elevation);
    w.u16(sat.azimuth < 0 ? 0xFFFF : static_cast<uint16_t>(sat.azimuth));
    w.u8(sat.snr);
  }
  return packetSize;
}

}  // namespace ObservationPacket
