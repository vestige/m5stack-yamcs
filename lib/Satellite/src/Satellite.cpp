#include "Satellite.h"

namespace {

using EventPacket::EventId;
using Telecommand::CommandId;
using Telecommand::ErrorCode;

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

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
  }
}

void Satellite::checkHealth(uint32_t nowMs, uint32_t lastGnssDataMs) {
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
}

void Satellite::changePayloadPower(bool on) {
  if (on == payloadPower_) return;
  payloadPower_ = on;
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
}

const char* Satellite::modeName(OperationMode mode) {
  switch (mode) {
    case OperationMode::Safe: return "SAFE";
    case OperationMode::Nominal: return "NOMINAL";
    case OperationMode::Mission: return "MISSION";
  }
  return "?";
}
