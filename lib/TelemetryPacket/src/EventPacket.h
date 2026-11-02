#pragma once

#include <stddef.h>
#include <stdint.h>

// 衛星で起きた出来事 (イベント) を CCSDS Space Packet (TM) に詰める。
// 衛星はイベントの番号と数値だけを送り、文章は地上 (YAMCSのMDBのアルゴリズム) で組み立てる。
//
// offset size  field
//      0    6  CCSDS primary header (secondary header flag=1) (APID=103)
//      6    8  uint64  packet time (機上時刻、Unix 時刻のミリ秒。わからなければ0)
//     14    1  uint8   severity (Severity)
//     15    1  uint8   event id (EventId)
//     16    2  uint16  arg1
//     18    2  uint16  arg2
namespace EventPacket {

const uint16_t kApid = 103;
const size_t kPacketSize = 20;

// YAMCSのイベントの重要度と同じ並び
enum class Severity : uint8_t { Info = 0, Watch = 1, Warning = 2, Distress = 3, Critical = 4, Severe = 5 };

enum class EventId : uint8_t {
  Boot = 1,                 // arg1: 再起動の回数, arg2: 再起動の理由
  ModeChanged = 2,          // arg1: 変更前のモード, arg2: 変更後のモード
  PayloadPowerChanged = 3,  // arg1: 0=OFF, 1=ON
  TmIntervalChanged = 4,    // arg1: 周期 (ms)
  CountersReset = 5,
  CommandRejected = 6,      // arg1: コマンドID, arg2: エラーコード
  FaultDetected = 7,        // arg1: 異常 (HousekeepingPacket::Fault), arg2: 異常が続いた秒数
  FdirChanged = 8,          // arg1: 0=OFF, 1=ON
  ObservationScheduled = 9,   // arg1: 開始までの秒数 (65535で頭打ち), arg2: 観測の長さ (秒)
  ObservationStarted = 10,    // arg1: 観測の長さ (秒)
  ObservationEnded = 11,      // arg1: 0=完了, 1=中断
  ObservationSkipped = 12,    // arg1: 1=ペイロードがOFF, 2=NOMINALでない (arg2: そのときのモード)
  ObservationCancelled = 13,  // arg1: 0=予約を取り消した, 1=観測中に取り消した
  LampOn = 14,                // arg1: 色 (LampColor), arg2: 点灯時間 (秒)
  LampOff = 15,               // arg1: 消す前の色, arg2: 0=コマンドで消した, 1=時間が来た, 2=SAFEになった, 3=ペイロードの電源が切れた
  RecorderStateChanged = 16,  // arg1: データレコーダの状態 (0=NO_CARD, 1=RECORDING, 2=WAITING_TIME, 3=ERROR), arg2: 空き (MB)
};

struct Event {
  Severity severity = Severity::Info;
  EventId id = EventId::Boot;
  uint16_t arg1 = 0;
  uint16_t arg2 = 0;
};

size_t encode(const Event& event, uint16_t sequenceCount, uint64_t timeMs, uint8_t* buffer, size_t size);

}  // namespace EventPacket
