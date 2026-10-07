#pragma once

#include <AckPacket.h>
#include <Satellite.h>

#include "GroundLink.h"

// 地上から届いたTCを衛星に実行させ、受理・拒否・完了をACKで返す
class CommandHandler {
 public:
  CommandHandler(Satellite& satellite, GroundLink& link) : satellite_(satellite), link_(link) {}

  void poll();

 private:
  static const size_t kMaxTelecommandSize = 64;
  static const uint16_t kBeepFrequencyHz = 2000;

  // BEEPのように時間のかかるコマンドは、終わってから COMPLETED を返す
  struct PendingCompletion {
    bool active = false;
    uint16_t tcSequence = 0;
    uint8_t commandId = 0;
    uint32_t dueMs = 0;
  };

  void handle(const uint8_t* data, size_t size);
  void startBeep(const Telecommand::Packet& tc, uint16_t durationMs);
  void completePending();
  void ack(uint16_t tcSequence, uint8_t commandId, AckPacket::Stage stage,
           Telecommand::ErrorCode error = Telecommand::ErrorCode::None);

  Satellite& satellite_;
  GroundLink& link_;
  PendingCompletion pending_;
};
