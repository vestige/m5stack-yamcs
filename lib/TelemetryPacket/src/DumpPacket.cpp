#include "DumpPacket.h"

#include <string.h>

namespace DumpPacket {

size_t encode(const uint8_t* recorded, size_t recordedSize, uint16_t sequenceCount, uint64_t timeMs, uint8_t* buffer,
              size_t size) {
  size_t packetSize = PacketWriter::kHeaderSize + recordedSize;
  if (recordedSize == 0 || recordedSize > kMaxRecordedSize || size < packetSize) return 0;

  PacketWriter w(buffer);
  w.header(kApid, sequenceCount, packetSize, timeMs);
  memcpy(buffer + PacketWriter::kHeaderSize, recorded, recordedSize);
  return packetSize;
}

}  // namespace DumpPacket
