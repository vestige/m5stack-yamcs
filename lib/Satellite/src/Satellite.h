#pragma once

#include <HousekeepingPacket.h>
#include <stdint.h>

#include "Telecommand.h"

// 衛星M5Satのバスの状態 (運用モード・ペイロード電源・コマンドカウンタなど) と、
// 地上から届いたコマンドの実行を受け持つ。Arduinoに依存しない。
class Satellite {
 public:
  using OperationMode = HousekeepingPacket::OperationMode;

  static const uint16_t kMinTmIntervalMs = 200;
  static const uint16_t kMaxTmIntervalMs = 10000;
  static const uint16_t kDefaultTmIntervalMs = 1000;
  static const uint16_t kMinBeepMs = 50;
  static const uint16_t kMaxBeepMs = 2000;

  // コマンドを処理した結果
  struct Result {
    bool accepted = false;
    Telecommand::ErrorCode error = Telecommand::ErrorCode::None;
    // BEEPを受理したとき、鳴らす長さ (ms)。0なら鳴らさない
    uint16_t beepMs = 0;
  };

  // コマンドを検査し、受理できれば実行する。受理数・拒否数もここで数える。
  Result handle(const Telecommand::Packet& tc);
  // TCパケットとして解析できなかったものを拒否として数える
  void countRejected() { rejectedCommands_++; }

  OperationMode mode() const { return mode_; }
  bool payloadPower() const { return payloadPower_; }
  uint16_t tmIntervalMs() const { return tmIntervalMs_; }
  uint16_t acceptedCommands() const { return acceptedCommands_; }
  uint16_t rejectedCommands() const { return rejectedCommands_; }
  uint8_t lastCommandId() const { return lastCommandId_; }

  // HKのうちバスの状態に関する項目を埋める
  void fillHousekeeping(HousekeepingPacket::Housekeeping* hk) const;

  static const char* modeName(OperationMode mode);

 private:
  Telecommand::ErrorCode validate(const Telecommand::Packet& tc) const;
  void execute(const Telecommand::Packet& tc, Result* result);

  OperationMode mode_ = OperationMode::Nominal;
  bool payloadPower_ = true;
  uint16_t tmIntervalMs_ = kDefaultTmIntervalMs;
  uint16_t acceptedCommands_ = 0;
  uint16_t rejectedCommands_ = 0;
  uint8_t lastCommandId_ = 0;
};
