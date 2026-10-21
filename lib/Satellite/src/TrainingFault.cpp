#include "TrainingFault.h"

namespace TrainingFault {

namespace {

const size_t kPrimaryHeaderSize = 6;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

}  // namespace

bool isTrainingPacket(const uint8_t* data, size_t size) {
  if (size < kPrimaryHeaderSize) return false;
  uint16_t word1 = readU16(data);
  bool isTelecommand = (word1 >> 12) & 1;
  return isTelecommand && (word1 & 0x07FF) == kApid;
}

bool parse(const uint8_t* data, size_t size, Request* request) {
  if (!isTrainingPacket(data, size) || size != kPacketSize) return false;
  if (readU16(data + 4) + 1 + kPrimaryHeaderSize != size) return false;
  uint8_t kind = data[6];
  if (kind > static_cast<uint8_t>(Kind::Reboot)) return false;
  request->kind = static_cast<Kind>(kind);
  request->durationSec = readU16(data + 7);
  return true;
}

size_t encode(const Request& request, uint16_t sequenceCount, uint8_t* buffer, size_t size) {
  if (size < kPacketSize) return 0;
  uint16_t word1 = (1 << 12) | kApid;
  uint16_t word2 = 0xC000 | (sequenceCount & 0x3FFF);
  uint16_t length = kPacketSize - kPrimaryHeaderSize - 1;
  buffer[0] = word1 >> 8;
  buffer[1] = word1 & 0xFF;
  buffer[2] = word2 >> 8;
  buffer[3] = word2 & 0xFF;
  buffer[4] = length >> 8;
  buffer[5] = length & 0xFF;
  buffer[6] = static_cast<uint8_t>(request.kind);
  buffer[7] = request.durationSec >> 8;
  buffer[8] = request.durationSec & 0xFF;
  return kPacketSize;
}

}  // namespace TrainingFault

void TrainingFaults::apply(const TrainingFault::Request& request, uint32_t nowMs) {
  uint32_t untilMs = nowMs + static_cast<uint32_t>(request.durationSec) * 1000;
  switch (request.kind) {
    case TrainingFault::Kind::Clear:
      gnssStop_ = false;
      commandLoss_ = false;
      rebootRequested_ = false;
      break;
    case TrainingFault::Kind::GnssStop:
      gnssStop_ = true;
      gnssStopUntilMs_ = untilMs;
      break;
    case TrainingFault::Kind::CommandLoss:
      commandLoss_ = true;
      commandLossUntilMs_ = untilMs;
      break;
    case TrainingFault::Kind::Reboot:
      rebootRequested_ = true;
      break;
  }
}

bool TrainingFaults::takeRebootRequest() {
  bool requested = rebootRequested_;
  rebootRequested_ = false;
  return requested;
}
