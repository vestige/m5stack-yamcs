#include "Satellite.h"

namespace {

using EventPacket::EventId;
using Telecommand::CommandId;
using Telecommand::ErrorCode;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t readU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// コマンドごとの引数のバイト数。知らないコマンドは -1
int argsLengthOf(uint8_t commandId) {
  switch (static_cast<CommandId>(commandId)) {
    case CommandId::NoOp: return 0;
    case CommandId::SetMode: return 1;
    case CommandId::SetTmInterval: return 2;
    case CommandId::PayloadPower: return 1;
    case CommandId::ResetCounters: return 0;
    case CommandId::Beep: return 2;
    case CommandId::SetFdir: return 1;
    case CommandId::ScheduleObservation: return 6;
    case CommandId::CancelObservation: return 0;
    case CommandId::SetLamp: return 3;
  }
  return -1;
}

}  // namespace

Satellite::Result Satellite::handle(const Telecommand::Packet& tc) {
  Result result;
  result.error = validate(tc);
  if (result.error != ErrorCode::None) {
    reject(tc.commandId, result.error);
    return result;
  }

  result.accepted = true;
  acceptedCommands_++;
  lastCommandId_ = tc.commandId;
  execute(tc, &result);
  return result;
}

void Satellite::reject(uint8_t commandId, ErrorCode error) {
  rejectedCommands_++;
  EventPacket::Event event;
  event.severity = EventPacket::Severity::Warning;
  event.id = EventId::CommandRejected;
  event.arg1 = commandId;
  event.arg2 = static_cast<uint16_t>(error);
  report(event);
}

ErrorCode Satellite::validate(const Telecommand::Packet& tc) const {
  int expectedLength = argsLengthOf(tc.commandId);
  if (expectedLength < 0) return ErrorCode::UnknownCommand;
  if (tc.argsLength != static_cast<size_t>(expectedLength)) return ErrorCode::InvalidLength;

  switch (static_cast<CommandId>(tc.commandId)) {
    case CommandId::SetMode: {
      uint8_t mode = tc.args[0];
      if (mode > static_cast<uint8_t>(OperationMode::Mission)) return ErrorCode::InvalidArgument;
      // ペイロードがOFFのままでは観測(MISSION)を始められない
      if (static_cast<OperationMode>(mode) == OperationMode::Mission && !payloadPower_) {
        return ErrorCode::NotAllowed;
      }
      return ErrorCode::None;
    }
    case CommandId::SetTmInterval: {
      uint16_t interval = readU16(tc.args);
      if (interval < kMinTmIntervalMs || interval > kMaxTmIntervalMs) return ErrorCode::InvalidArgument;
      return ErrorCode::None;
    }
    case CommandId::PayloadPower:
    case CommandId::SetFdir:
      if (tc.args[0] > 1) return ErrorCode::InvalidArgument;
      return ErrorCode::None;
    case CommandId::Beep: {
      uint16_t duration = readU16(tc.args);
      if (duration < kMinBeepMs || duration > kMaxBeepMs) return ErrorCode::InvalidArgument;
      return ErrorCode::None;
    }
    case CommandId::ScheduleObservation: {
      uint32_t start = readU32(tc.args);
      uint16_t duration = readU16(tc.args + 4);
      if (duration < kMinObservationSec || duration > kMaxObservationSec) return ErrorCode::InvalidArgument;
      // 機上時刻がわからなければ、時刻指定のコマンドは実行できない
      if (!clock_.valid()) return ErrorCode::NotAllowed;
      // 観測中は予約し直せない (先に取り消す)
      if (observationState_ == ObservationState::Active) return ErrorCode::NotAllowed;
      if (start <= clock_.now(nowMs_)) return ErrorCode::InvalidArgument;
      return ErrorCode::None;
    }
    case CommandId::CancelObservation:
      if (observationState_ == ObservationState::None) return ErrorCode::NotAllowed;
      return ErrorCode::None;
    case CommandId::SetLamp: {
      uint8_t color = tc.args[0];
      uint16_t duration = readU16(tc.args + 1);
      if (color > static_cast<uint8_t>(LampColor::White)) return ErrorCode::InvalidArgument;
      if (static_cast<LampColor>(color) == LampColor::Off) return ErrorCode::None;  // 消すのはいつでもよい
      if (duration < 1 || duration > kMaxLampSec) return ErrorCode::InvalidArgument;
      // SAFE は異常からの退避中なので、ペイロードの操作 (ミッション要求) は受けない
      if (mode_ == OperationMode::Safe) return ErrorCode::NotAllowed;
      // ランプはペイロードの機器なので、電源が入っていなければ点けられない
      if (!payloadPower_) return ErrorCode::NotAllowed;
      return ErrorCode::None;
    }
    default:
      return ErrorCode::None;
  }
}

void Satellite::execute(const Telecommand::Packet& tc, Result* result) {
  switch (static_cast<CommandId>(tc.commandId)) {
    case CommandId::NoOp:
      break;
    case CommandId::SetMode: {
      OperationMode mode = static_cast<OperationMode>(tc.args[0]);
      changeMode(mode);
      // SAFEでは最低限の機能だけ動かすので、ペイロードを切る
      if (mode == OperationMode::Safe) changePayloadPower(false);
      break;
    }
    case CommandId::SetTmInterval:
      tmIntervalMs_ = readU16(tc.args);
      reportInfo(EventId::TmIntervalChanged, tmIntervalMs_);
      break;
    case CommandId::PayloadPower:
      changePayloadPower(tc.args[0] == 1);
      // 観測中にペイロードを切ったら観測は続けられないので NOMINAL に戻す
      if (!payloadPower_ && mode_ == OperationMode::Mission) changeMode(OperationMode::Nominal);
      break;
    case CommandId::ResetCounters:
      acceptedCommands_ = 0;
      rejectedCommands_ = 0;
      reportInfo(EventId::CountersReset);
      break;
    case CommandId::Beep:
      result->beepMs = readU16(tc.args);
      break;
    case CommandId::SetFdir: {
      bool enabled = tc.args[0] == 1;
      if (enabled == fdirEnabled_) break;
      fdirEnabled_ = enabled;
      // 有効にした時点から、改めて猶予を置いて監視する
      watchingPayload_ = false;
      reportInfo(EventId::FdirChanged, enabled ? 1 : 0);
      break;
    }
    case CommandId::ScheduleObservation: {
      observationStart_ = readU32(tc.args);
      observationDuration_ = readU16(tc.args + 4);
      observationState_ = ObservationState::Scheduled;
      uint32_t untilStart = observationStart_ - clock_.now(nowMs_);
      reportInfo(EventId::ObservationScheduled, untilStart > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(untilStart),
                 observationDuration_);
      break;
    }
    case CommandId::SetLamp: {
      LampColor color = static_cast<LampColor>(tc.args[0]);
      if (color == LampColor::Off) {
        turnLampOff(kLampOffByCommand);
        break;
      }
      uint16_t duration = readU16(tc.args + 1);
      lampColor_ = color;
      lampUntilMs_ = nowMs_ + static_cast<uint32_t>(duration) * 1000;
      reportInfo(EventId::LampOn, static_cast<uint16_t>(color), duration);
      break;
    }
    case CommandId::CancelObservation: {
      bool wasActive = observationState_ == ObservationState::Active;
      uint32_t now = clock_.now(nowMs_);
      if (wasActive) {
        recordObservation(ObservationResult::Cancelled, observationStartedSec_, now);
      } else {
        recordObservation(ObservationResult::Cancelled, observationStart_, observationStart_);
      }
      observationState_ = ObservationState::None;
      if (wasActive && mode_ == OperationMode::Mission) changeMode(OperationMode::Nominal);
      reportInfo(EventId::ObservationCancelled, wasActive ? 1 : 0);
      break;
    }
  }
}

void Satellite::updateObservation(uint32_t nowMs) {
  nowMs_ = nowMs;
  if (observationState_ == ObservationState::None || !clock_.valid()) return;
  uint32_t now = clock_.now(nowMs);

  if (observationState_ == ObservationState::Scheduled) {
    if (now < observationStart_) return;
    if (!payloadPower_) {
      observationState_ = ObservationState::None;
      recordObservation(ObservationResult::SkippedPayloadOff, observationStart_, observationStart_);
      EventPacket::Event event;
      event.severity = EventPacket::Severity::Warning;
      event.id = EventId::ObservationSkipped;
      event.arg1 = kSkippedPayloadOff;
      report(event);
      return;
    }
    if (mode_ != OperationMode::Nominal) {
      observationState_ = ObservationState::None;
      recordObservation(ObservationResult::SkippedNotNominal, observationStart_, observationStart_);
      EventPacket::Event event;
      event.severity = EventPacket::Severity::Warning;
      event.id = EventId::ObservationSkipped;
      event.arg1 = kSkippedNotNominal;
      event.arg2 = static_cast<uint16_t>(mode_);
      report(event);
      return;
    }
    observationState_ = ObservationState::Active;
    observationStartedSec_ = now;
    changeMode(OperationMode::Mission);
    reportInfo(EventId::ObservationStarted, observationDuration_);
    return;
  }

  // 観測中: MISSIONでなくなったら中断、長さが過ぎたら完了
  if (mode_ != OperationMode::Mission) {
    endObservation(false);
  } else if (now >= observationStart_ + observationDuration_) {
    changeMode(OperationMode::Nominal);
    endObservation(true);
  }
}

void Satellite::endObservation(bool completed) {
  observationState_ = ObservationState::None;
  recordObservation(completed ? ObservationResult::Completed : ObservationResult::Aborted, observationStartedSec_,
                    clock_.now(nowMs_));
  if (completed) completedObservations_++;
  EventPacket::Event event;
  event.severity = completed ? EventPacket::Severity::Info : EventPacket::Severity::Warning;
  event.id = EventId::ObservationEnded;
  event.arg1 = completed ? 0 : 1;
  report(event);
}

void Satellite::recordObservation(ObservationResult result, uint32_t startSec, uint32_t endSec) {
  lastObservationResult_ = result;
  lastObservationStart_ = startSec;
  uint32_t length = endSec > startSec ? endSec - startSec : 0;
  lastObservationLength_ = length > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(length);
}

void Satellite::checkHealth(uint32_t nowMs, uint32_t lastGnssDataMs) {
  nowMs_ = nowMs;
  if (!payloadPower_) {
    watchingPayload_ = false;
    return;
  }
  if (!watchingPayload_) {
    watchingPayload_ = true;
    watchStartMs_ = nowMs;
  }
  if (!fdirEnabled_) return;

  // 監視を始めてから届いたデータだけを数える
  bool receivedSinceWatch =
      lastGnssDataMs != 0 && static_cast<int32_t>(lastGnssDataMs - watchStartMs_) >= 0;
  uint32_t lastAlive = receivedSinceWatch ? lastGnssDataMs : watchStartMs_;
  uint32_t timeoutMs = receivedSinceWatch ? kGnssTimeoutMs : kGnssStartupTimeoutMs;
  uint32_t silentMs = nowMs - lastAlive;
  if (silentMs >= timeoutMs) handleFault(Fault::GnssNoData, static_cast<uint16_t>(silentMs / 1000));
}

void Satellite::handleFault(Fault fault, uint16_t durationSec) {
  lastFault_ = fault;
  faultCount_++;
  EventPacket::Event event;
  event.severity = EventPacket::Severity::Distress;
  event.id = EventId::FaultDetected;
  event.arg1 = static_cast<uint16_t>(fault);
  event.arg2 = durationSec;
  report(event);

  // 分離: ペイロードを切る / 退避: SAFE へ
  changePayloadPower(false);
  changeMode(OperationMode::Safe);
  watchingPayload_ = false;
}

void Satellite::changeMode(OperationMode mode) {
  if (mode == mode_) return;
  reportInfo(EventId::ModeChanged, static_cast<uint16_t>(mode_), static_cast<uint16_t>(mode));
  mode_ = mode;
  if (mode == OperationMode::Safe) turnLampOff(kLampOffSafe);
}

void Satellite::updateLamp(uint32_t nowMs) {
  nowMs_ = nowMs;
  if (lampColor_ != LampColor::Off && static_cast<int32_t>(nowMs - lampUntilMs_) >= 0) turnLampOff(kLampOffExpired);
}

void Satellite::turnLampOff(uint16_t reason) {
  if (lampColor_ == LampColor::Off) return;
  LampColor previous = lampColor_;
  lampColor_ = LampColor::Off;
  reportInfo(EventId::LampOff, static_cast<uint16_t>(previous), reason);
}

uint16_t Satellite::payloadOnSec(uint32_t nowMs) const {
  if (!payloadPower_) return 0;
  uint32_t sec = (nowMs - payloadOnSinceMs_) / 1000;
  return sec > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(sec);
}

uint16_t Satellite::lampRemainingSec(uint32_t nowMs) const {
  if (lampColor_ == LampColor::Off) return 0;
  int32_t remainingMs = static_cast<int32_t>(lampUntilMs_ - nowMs);
  return remainingMs <= 0 ? 0 : static_cast<uint16_t>((remainingMs + 999) / 1000);
}

void Satellite::changePayloadPower(bool on) {
  if (on == payloadPower_) return;
  payloadPower_ = on;
  if (on) payloadOnSinceMs_ = nowMs_;
  if (!on) turnLampOff(kLampOffPayloadOff);
  reportInfo(EventId::PayloadPowerChanged, on ? 1 : 0);
}

void Satellite::reportInfo(EventId id, uint16_t arg1, uint16_t arg2) {
  EventPacket::Event event;
  event.severity = EventPacket::Severity::Info;
  event.id = id;
  event.arg1 = arg1;
  event.arg2 = arg2;
  report(event);
}

void Satellite::fillHousekeeping(HousekeepingPacket::Housekeeping* hk) const {
  hk->mode = mode_;
  hk->payloadPower = payloadPower_;
  hk->acceptedCommands = acceptedCommands_;
  hk->rejectedCommands = rejectedCommands_;
  hk->lastCommandId = lastCommandId_;
  hk->fdirEnabled = fdirEnabled_;
  hk->lastFault = lastFault_;
  hk->faultCount = faultCount_;
  hk->onboardTime = clock_.now(nowMs_);
  hk->observationState = observationState_;
  hk->observationStart = observationState_ == ObservationState::None ? 0 : observationStart_;
  hk->observationDuration = observationState_ == ObservationState::None ? 0 : observationDuration_;
  hk->lastObservationResult = lastObservationResult_;
  hk->lastObservationStart = lastObservationStart_;
  hk->lastObservationLength = lastObservationLength_;
  hk->completedObservations = completedObservations_;
  hk->lampColor = lampColor_;
  hk->lampRemaining = lampRemainingSec(nowMs_);
  hk->payloadOnTime = payloadOnSec(nowMs_);
}

const char* Satellite::modeName(OperationMode mode) {
  switch (mode) {
    case OperationMode::Safe: return "SAFE";
    case OperationMode::Nominal: return "NOMINAL";
    case OperationMode::Mission: return "MISSION";
  }
  return "?";
}
