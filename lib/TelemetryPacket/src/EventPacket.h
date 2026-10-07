#pragma once

#include <stddef.h>
#include <stdint.h>

// 衛星で起きた出来事 (イベント) を CCSDS Space Packet (TM) に詰める。
// 衛星はイベントの番号と数値だけを送り、文章は地上 (YAMCSのMDBのアルゴリズム) で組み立てる。
//
// offset size  field
//      0    6  CCSDS primary header (APID=103)
//      6    1  uint8   severity (Severity)
//      7    1  uint8   event id (EventId)
//      8    2  uint16  arg1
//     10    2  uint16  arg2
namespace EventPacket {

const uint16_t kApid = 103;
const size_t kPacketSize = 12;

// YAMCSのイベントの重要度と同じ並び
enum class Severity : uint8_t { Info = 0, Watch = 1, Warning = 2, Distress = 3, Critical = 4, Severe = 5 };

enum class EventId : uint8_t {
  Boot = 1,                 // arg1: 再起動の回数, arg2: 再起動の理由
  ModeChanged = 2,          // arg1: 変更前のモード, arg2: 変更後のモード
  PayloadPowerChanged = 3,  // arg1: 0=OFF, 1=ON
  TmIntervalChanged = 4,    // arg1: 周期 (ms)
  CountersReset = 5,
  CommandRejected = 6,      // arg1: コマンドID, arg2: エラーコード
};

struct Event {
  Severity severity = Severity::Info;
  EventId id = EventId::Boot;
  uint16_t arg1 = 0;
  uint16_t arg2 = 0;
};

size_t encode(const Event& event, uint16_t sequenceCount, uint8_t* buffer, size_t size);

}  // namespace EventPacket
