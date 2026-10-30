#pragma once

#include <stddef.h>
#include <stdint.h>

// 衛星M5SatのHK (Housekeeping) テレメトリを CCSDS Space Packet (TM) に詰める。
// 数値はすべてビッグエンディアン。
//
// offset size  field
//      0    6  CCSDS primary header (secondary header flag=1) (APID=101)
//      6    8  uint64  packet time (機上時刻、Unix 時刻のミリ秒。わからなければ0)
//     14    1  uint8   operation mode (OperationMode)
//     15    1  uint8   payload power (0=OFF, 1=ON)
//     16    2  uint16  accepted command count
//     18    2  uint16  rejected command count
//     20    1  uint8   last command id (0=なし)
//     21    1  int8    Wi-Fi RSSI (dBm)
//     22    4  uint32  free heap (bytes)
//     26    4  uint32  minimum free heap since boot (bytes)
//     30    4  uint32  uptime (ms)
//     34    2  uint16  boot count
//     36    1  uint8   reset reason (ESP-IDF の esp_reset_reason_t)
//     37    1  uint8   FDIR enabled (0=OFF, 1=ON)
//     38    1  uint8   last fault (Fault)
//     39    2  uint16  fault count
//     41    4  uint32  onboard time (Unix秒。まだ一度も合わせていなければ0)
//     45    1  uint8   observation state (ObservationState)
//     46    4  uint32  observation start (Unix秒。予約がなければ0)
//     50    2  uint16  observation duration (秒)
//     52    1  uint8   last observation result (ObservationResult)
//     53    4  uint32  last observation start (Unix秒。実際に始めた時刻。見送り・開始前の取り消しは予約の時刻)
//     57    2  uint16  last observation length (秒。実際に観測した長さ)
//     59    2  uint16  completed observation count
//     61    1  uint8   lamp color (LampColor。OFF=消えている)
//     62    2  uint16  lamp remaining (秒。消えていれば0)
//     64    2  uint16  payload on time (秒。ペイロードの電源を入れてからの時間。OFF なら0、65535 で頭打ち)
namespace HousekeepingPacket {

const uint16_t kApid = 101;
const size_t kPacketSize = 66;

enum class OperationMode : uint8_t { Safe = 0, Nominal = 1, Mission = 2 };

// FDIRが検知した異常
enum class Fault : uint8_t { None = 0, GnssNoData = 1 };

// 時刻指定の観測の状態
enum class ObservationState : uint8_t { None = 0, Scheduled = 1, Active = 2 };

// 最後の観測がどうなったか。通信できない間に観測しても、次のパスでHKを見れば結果がわかる
enum class ObservationResult : uint8_t {
  None = 0,               // まだ一度も観測していない
  Completed = 1,          // 予約した長さを観測し終えた
  Aborted = 2,            // 観測中に MISSION でなくなり、中断した
  SkippedPayloadOff = 3,  // 開始の時刻にペイロードがOFFで、見送った
  SkippedNotNominal = 4,  // 開始の時刻に NOMINAL でなく、見送った
  Cancelled = 5,          // CANCEL_OBSERVATION で取り消した
};

// ミッション要求で点けるランプの色 (SET_LAMP)
enum class LampColor : uint8_t { Off = 0, Red = 1, Green = 2, Blue = 3, Yellow = 4, Cyan = 5, Magenta = 6, White = 7 };

struct Housekeeping {
  OperationMode mode = OperationMode::Nominal;
  bool payloadPower = true;
  uint16_t acceptedCommands = 0;
  uint16_t rejectedCommands = 0;
  uint8_t lastCommandId = 0;
  int8_t wifiRssi = 0;
  uint32_t freeHeap = 0;
  uint32_t minFreeHeap = 0;
  uint32_t uptimeMs = 0;
  uint16_t bootCount = 0;
  uint8_t resetReason = 0;
  bool fdirEnabled = true;
  Fault lastFault = Fault::None;
  uint16_t faultCount = 0;
  uint32_t onboardTime = 0;
  ObservationState observationState = ObservationState::None;
  uint32_t observationStart = 0;
  uint16_t observationDuration = 0;
  ObservationResult lastObservationResult = ObservationResult::None;
  uint32_t lastObservationStart = 0;
  uint16_t lastObservationLength = 0;
  uint16_t completedObservations = 0;
  LampColor lampColor = LampColor::Off;
  uint16_t lampRemaining = 0;
  uint16_t payloadOnTime = 0;
};

// パケットをbufferに書き込み、書き込んだバイト数を返す。bufferが小さければ0を返す。
// sequenceCount は下位14bitが使われる（APIDごとに数える）。
size_t encode(const Housekeeping& hk, uint16_t sequenceCount, uint64_t timeMs, uint8_t* buffer, size_t size);

}  // namespace HousekeepingPacket
