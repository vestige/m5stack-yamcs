#include "Satellite.h"

namespace {

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
  }
  return -1;
}

}  // namespace

Satellite::Result Satellite::handle(const Telecommand::Packet& tc) {
  Result result;
  result.error = validate(tc);
  if (result.error != ErrorCode::None) {
    rejectedCommands_++;
    return result;
  }

  result.accepted = true;
  acceptedCommands_++;
  lastCommandId_ = tc.commandId;
  execute(tc, &result);
  return result;
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
    case CommandId::SetMode:
      mode_ = static_cast<OperationMode>(tc.args[0]);
      // SAFEでは最低限の機能だけ動かすので、ペイロードを切る
      if (mode_ == OperationMode::Safe) payloadPower_ = false;
      break;
    case CommandId::SetTmInterval:
      tmIntervalMs_ = readU16(tc.args);
      break;
    case CommandId::PayloadPower:
      payloadPower_ = tc.args[0] == 1;
      // 観測中にペイロードを切ったら観測は続けられないので NOMINAL に戻す
      if (!payloadPower_ && mode_ == OperationMode::Mission) mode_ = OperationMode::Nominal;
      break;
    case CommandId::ResetCounters:
      acceptedCommands_ = 0;
      rejectedCommands_ = 0;
      break;
    case CommandId::Beep:
      result->beepMs = readU16(tc.args);
      break;
  }
}

void Satellite::fillHousekeeping(HousekeepingPacket::Housekeeping* hk) const {
  hk->mode = mode_;
  hk->payloadPower = payloadPower_;
  hk->acceptedCommands = acceptedCommands_;
  hk->rejectedCommands = rejectedCommands_;
  hk->lastCommandId = lastCommandId_;
}

const char* Satellite::modeName(OperationMode mode) {
  switch (mode) {
    case OperationMode::Safe: return "SAFE";
    case OperationMode::Nominal: return "NOMINAL";
    case OperationMode::Mission: return "MISSION";
  }
  return "?";
}
