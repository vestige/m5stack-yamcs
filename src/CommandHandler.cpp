#include "CommandHandler.h"

#include <M5Unified.h>
#include <Telecommand.h>

void CommandHandler::poll() {
  uint8_t buffer[kMaxTelecommandSize];
  size_t size;
  while ((size = link_.receiveTelecommand(buffer, sizeof(buffer))) > 0) {
    // バッファより長いTCは、長さフィールドの検査で拒否される
    handle(buffer, size);
  }

  if (pending_.active && static_cast<int32_t>(millis() - pending_.dueMs) >= 0) completePending();
}

void CommandHandler::handle(const uint8_t* data, size_t size) {
  Telecommand::Packet tc;
  bool hasHeader = false;
  Telecommand::ErrorCode parseError = Telecommand::parse(data, size, &tc, &hasHeader);
  if (parseError != Telecommand::ErrorCode::None) {
    satellite_.countRejected();
    if (hasHeader) {
      uint8_t commandId = size > Telecommand::kPrimaryHeaderSize ? data[Telecommand::kPrimaryHeaderSize] : 0;
      ack(tc.sequenceCount, commandId, AckPacket::Stage::Rejected, parseError);
    }
    return;
  }

  Satellite::Result result = satellite_.handle(tc);
  if (!result.accepted) {
    ack(tc.sequenceCount, tc.commandId, AckPacket::Stage::Rejected, result.error);
    return;
  }

  ack(tc.sequenceCount, tc.commandId, AckPacket::Stage::Accepted);
  if (result.beepMs > 0) {
    startBeep(tc, result.beepMs);
  } else {
    ack(tc.sequenceCount, tc.commandId, AckPacket::Stage::Completed);
  }
}

void CommandHandler::startBeep(const Telecommand::Packet& tc, uint16_t durationMs) {
  completePending();
  M5.Speaker.tone(kBeepFrequencyHz, durationMs);
  pending_.active = true;
  pending_.tcSequence = tc.sequenceCount;
  pending_.commandId = tc.commandId;
  pending_.dueMs = millis() + durationMs;
}

void CommandHandler::completePending() {
  if (!pending_.active) return;
  pending_.active = false;
  ack(pending_.tcSequence, pending_.commandId, AckPacket::Stage::Completed);
}

void CommandHandler::ack(uint16_t tcSequence, uint8_t commandId, AckPacket::Stage stage,
                         Telecommand::ErrorCode error) {
  link_.sendAck(tcSequence, commandId, stage, static_cast<uint8_t>(error));
  Serial.printf("[TC] seq=%u id=%u stage=%u error=%u\n", tcSequence, commandId,
                static_cast<unsigned>(stage), static_cast<unsigned>(error));
}
