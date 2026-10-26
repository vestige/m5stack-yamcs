#pragma once

#include <stddef.h>
#include <stdint.h>

// 地上から届く TC (Telecommand) パケット。
//
// offset size  field
//      0    6  CCSDS primary header (version=0, type=TC, APID=110)
//      6    1  uint8   command id (CommandId)
//      7    -  引数 (コマンドごと, ビッグエンディアン)
namespace Telecommand {

const uint16_t kApid = 110;
const size_t kPrimaryHeaderSize = 6;

enum class CommandId : uint8_t {
  NoOp = 1,
  SetMode = 2,
  SetTmInterval = 3,
  PayloadPower = 4,
  ResetCounters = 5,
  Beep = 6,
  SetFdir = 7,
  ScheduleObservation = 8,  // uint32 開始時刻 (Unix秒), uint16 長さ (秒)
  CancelObservation = 9,
  SetLamp = 10,  // uint8 色 (LampColor), uint16 点灯時間 (秒)。ミッション要求で使う
};

// コマンドを拒否・失敗した理由 (ACKで地上へ返す)
enum class ErrorCode : uint8_t {
  None = 0,
  UnknownCommand = 1,     // 知らないコマンドID
  InvalidLength = 2,      // 引数の長さがコマンドと合わない
  InvalidArgument = 3,    // 引数の値が範囲外
  NotAllowed = 4,         // 今の状態では実行できない
  MalformedPacket = 5,    // TCパケットとして正しくない (APID, 種別, 長さフィールド)
};

struct Packet {
  uint16_t sequenceCount = 0;
  uint8_t commandId = 0;
  const uint8_t* args = nullptr;
  size_t argsLength = 0;
};

// TCパケットを解析する。ヘッダが読めればシーケンス番号は out に入る (拒否のACKを返すため)。
// hasHeader: シーケンス番号が読めたか
ErrorCode parse(const uint8_t* data, size_t size, Packet* out, bool* hasHeader);

}  // namespace Telecommand
