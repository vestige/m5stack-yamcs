#pragma once

#include <EventPacket.h>
#include <HousekeepingPacket.h>
#include <stdint.h>

#include "EventQueue.h"
#include "OnboardClock.h"
#include "Telecommand.h"

// 衛星M5Satのバスの状態 (運用モード・ペイロード電源・コマンドカウンタなど) と、
// 地上から届いたコマンドの実行を受け持つ。Arduinoに依存しない。
class Satellite {
 public:
  using OperationMode = HousekeepingPacket::OperationMode;
  using Fault = HousekeepingPacket::Fault;
  using ObservationState = HousekeepingPacket::ObservationState;
  using ObservationResult = HousekeepingPacket::ObservationResult;
  using LampColor = HousekeepingPacket::LampColor;

  static const uint16_t kMinTmIntervalMs = 200;
  static const uint16_t kMaxTmIntervalMs = 10000;
  static const uint16_t kDefaultTmIntervalMs = 1000;
  static const uint16_t kMinBeepMs = 50;
  static const uint16_t kMaxBeepMs = 2000;
  // ペイロードの電源がONなのに、この時間GNSSのデータが届かなければ異常とみなす
  static const uint32_t kGnssTimeoutMs = 10000;
  // 電源を入れてから最初のデータが届くまでは、機器が動き出すのを長めに待つ
  static const uint32_t kGnssStartupTimeoutMs = 30000;
  static const uint16_t kMinObservationSec = 10;
  static const uint16_t kMaxObservationSec = 3600;
  static const uint16_t kMaxLampSec = 600;

  // ランプを消した理由 (LampOff の arg2)
  static const uint16_t kLampOffByCommand = 0;
  static const uint16_t kLampOffExpired = 1;
  static const uint16_t kLampOffSafe = 2;
  static const uint16_t kLampOffPayloadOff = 3;

  // 観測を見送った理由 (ObservationSkipped の arg1)
  static const uint16_t kSkippedPayloadOff = 1;
  static const uint16_t kSkippedNotNominal = 2;

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
  void reject(uint8_t commandId, Telecommand::ErrorCode error);

  // FDIR: 異常を検知したら、ペイロードを切ってSAFEへ退避する。
  // lastGnssDataMs: GNSSのデータが最後に届いた時刻 (まだ届いていなければ0)
  void checkHealth(uint32_t nowMs, uint32_t lastGnssDataMs);

  // 機上時刻をGPSの時刻で合わせる
  void syncTime(uint32_t utcSec, uint32_t nowMs) { clock_.sync(utcSec, nowMs); }
  // 時刻指定の観測: 予約した時刻に MISSION へ移り、長さが過ぎたら NOMINAL へ戻る
  void updateObservation(uint32_t nowMs);
  // ミッション要求のランプ: 点灯時間が過ぎたら消す
  void updateLamp(uint32_t nowMs);

  // 地上へ知らせるイベントを溜める / 取り出す
  void report(const EventPacket::Event& event) { events_.push(event); }
  bool popEvent(EventPacket::Event* event) { return events_.pop(event); }

  OperationMode mode() const { return mode_; }
  bool payloadPower() const { return payloadPower_; }
  uint16_t tmIntervalMs() const { return tmIntervalMs_; }
  uint16_t acceptedCommands() const { return acceptedCommands_; }
  uint16_t rejectedCommands() const { return rejectedCommands_; }
  uint8_t lastCommandId() const { return lastCommandId_; }
  bool fdirEnabled() const { return fdirEnabled_; }
  Fault lastFault() const { return lastFault_; }
  uint16_t faultCount() const { return faultCount_; }
  const OnboardClock& clock() const { return clock_; }
  ObservationState observationState() const { return observationState_; }
  ObservationResult lastObservationResult() const { return lastObservationResult_; }
  LampColor lampColor() const { return lampColor_; }
  // ペイロードの電源を入れてからの秒数 (OFF なら0)。立ち上がり中かを地上で判断するため HK で送る
  uint16_t payloadOnSec(uint32_t nowMs) const;
  // ランプが消えるまでの秒数 (消えていれば0)
  uint16_t lampRemainingSec(uint32_t nowMs) const;

  // HKのうちバスの状態に関する項目を埋める
  void fillHousekeeping(HousekeepingPacket::Housekeeping* hk) const;

  static const char* modeName(OperationMode mode);

 private:
  Telecommand::ErrorCode validate(const Telecommand::Packet& tc) const;
  void execute(const Telecommand::Packet& tc, Result* result);
  void changeMode(OperationMode mode);
  void changePayloadPower(bool on);
  void reportInfo(EventPacket::EventId id, uint16_t arg1 = 0, uint16_t arg2 = 0);
  void handleFault(Fault fault, uint16_t durationSec);
  void endObservation(bool completed);
  // 最後の観測の結果を覚える (HKで送り続ける)
  void recordObservation(ObservationResult result, uint32_t startSec, uint32_t endSec);
  void turnLampOff(uint16_t reason);

  OperationMode mode_ = OperationMode::Nominal;
  bool payloadPower_ = true;
  // ペイロードの電源を入れた時刻。起動時は電源が入っているので0 (起動した時刻)
  uint32_t payloadOnSinceMs_ = 0;
  uint16_t tmIntervalMs_ = kDefaultTmIntervalMs;
  uint16_t acceptedCommands_ = 0;
  uint16_t rejectedCommands_ = 0;
  uint8_t lastCommandId_ = 0;

  bool fdirEnabled_ = true;
  Fault lastFault_ = Fault::None;
  uint16_t faultCount_ = 0;
  // ペイロードを監視しはじめた時刻。電源を入れてから kGnssTimeoutMs は待つため
  bool watchingPayload_ = false;
  uint32_t watchStartMs_ = 0;

  // コマンドの検査や機上時刻に使う、直近の時刻 (checkHealth / updateObservation で更新する)
  uint32_t nowMs_ = 0;
  OnboardClock clock_;
  ObservationState observationState_ = ObservationState::None;
  uint32_t observationStart_ = 0;
  uint16_t observationDuration_ = 0;
  // 観測を実際に始めた時刻 (Unix秒)
  uint32_t observationStartedSec_ = 0;
  ObservationResult lastObservationResult_ = ObservationResult::None;
  uint32_t lastObservationStart_ = 0;
  uint16_t lastObservationLength_ = 0;
  uint16_t completedObservations_ = 0;

  LampColor lampColor_ = LampColor::Off;
  uint32_t lampUntilMs_ = 0;

  EventQueue events_;
};
