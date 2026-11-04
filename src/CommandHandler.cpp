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
  if (TrainingFault::isTrainingPacket(data, size)) {
    TrainingFault::Request request;
    if (TrainingFault::parse(data, size, &request)) {
      training_.apply(request, millis());
      Serial.printf("[TRAINING] kind=%u duration=%us\n", static_cast<unsigned>(request.kind), request.durationSec);
    } else {
      Serial.println("[TRAINING] invalid packet");
    }
    return;
  }
  // 訓練用の異常「コマンドの無視」: 受け取ったことにせず、ACKも返さない
  if (training_.commandsIgnored(millis())) {
    Serial.println("[TRAINING] command ignored");
    return;
  }

  Telecommand::Packet tc;
  bool hasHeader = false;
  Telecommand::ErrorCode parseError = Telecommand::parse(data, size, &tc, &hasHeader);
  if (parseError != Telecommand::ErrorCode::None) {
    uint8_t commandId = size > Telecommand::kPrimaryHeaderSize ? data[Telecommand::kPrimaryHeaderSize] : 0;
    satellite_.reject(commandId, parseError);
    if (hasHeader) ack(tc.sequenceCount, commandId, AckPacket::Stage::Rejected, parseError);
    return;
  }

  Satellite::Result result = satellite_.handle(tc);
  if (!result.accepted) {
    ack(tc.sequenceCount, tc.commandId, AckPacket::Stage::Rejected, result.error);
    return;
  }

  ack(tc.sequenceCount, tc.commandId, AckPacket::Stage::Accepted);
  if (result.recorder.kind != Satellite::RecorderRequest::Kind::None) {
    Telecommand::ErrorCode error = runRecorder(result.recorder);
    ack(tc.sequenceCount, tc.commandId,
        error == Telecommand::ErrorCode::None ? AckPacket::Stage::Completed : AckPacket::Stage::Failed, error);
  } else if (result.beepMs > 0) {
    startBeep(tc, result.beepMs);
  } else {
    ack(tc.sequenceCount, tc.commandId, AckPacket::Stage::Completed);
  }
}

Telecommand::ErrorCode CommandHandler::runRecorder(const Satellite::RecorderRequest& request) {
  using Kind = Satellite::RecorderRequest::Kind;
  uint8_t source = static_cast<uint8_t>(request.source);
  uint32_t now = satellite_.clock().now(millis());
  switch (request.kind) {
    case Kind::Dump: {
      // 終わり=0 は「今」。機上時刻がわからなければ、記録の終わりまで
      uint32_t end = request.endSec == 0 ? now : request.endSec;
      uint32_t totalBytes = 0;
      if (!recorder_.startDump(source, request.startSec, end, &totalBytes)) return Telecommand::ErrorCode::NotAllowed;
      uint32_t totalKB = (totalBytes + 1023) / 1024;
      EventPacket::Event event;
      event.id = EventPacket::EventId::DumpStarted;
      event.arg1 = source;
      event.arg2 = totalKB > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(totalKB);
      satellite_.report(event);
      return Telecommand::ErrorCode::None;
    }
    case Kind::StopDump:
      return recorder_.stopDump() ? Telecommand::ErrorCode::None : Telecommand::ErrorCode::NotAllowed;
    case Kind::Clear: {
      // これから記録する分 (未来) は、まだ降ろせていないので消さない
      uint32_t before = now != 0 && request.endSec > now ? now : request.endSec;
      int removed = recorder_.clear(source, before);
      if (removed < 0) return Telecommand::ErrorCode::NotAllowed;
      EventPacket::Event event;
      event.id = EventPacket::EventId::RecorderCleared;
      event.arg1 = source;
      event.arg2 = static_cast<uint16_t>(removed);
      satellite_.report(event);
      return Telecommand::ErrorCode::None;
    }
    case Kind::None:
      break;
  }
  return Telecommand::ErrorCode::None;
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
