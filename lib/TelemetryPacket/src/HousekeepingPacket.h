#pragma once

#include <stddef.h>
#include <stdint.h>

// 衛星M5SatのHK (Housekeeping) テレメトリを CCSDS Space Packet (TM) に詰める。
// 数値はすべてビッグエンディアン。
//
// offset size  field
//      0    6  CCSDS primary header (APID=101)
//      6    1  uint8   operation mode (OperationMode)
//      7    1  uint8   payload power (0=OFF, 1=ON)
//      8    2  uint16  accepted command count
//     10    2  uint16  rejected command count
//     12    1  uint8   last command id (0=なし)
//     13    1  int8    Wi-Fi RSSI (dBm)
//     14    4  uint32  free heap (bytes)
//     18    4  uint32  minimum free heap since boot (bytes)
//     22    4  uint32  uptime (ms)
//     26    2  uint16  boot count
//     28    1  uint8   reset reason (ESP-IDF の esp_reset_reason_t)
//     29    1  uint8   FDIR enabled (0=OFF, 1=ON)
//     30    1  uint8   last fault (Fault)
//     31    2  uint16  fault count
//     33    4  uint32  onboard time (Unix秒。まだ一度も合わせていなければ0)
//     37    1  uint8   observation state (ObservationState)
//     38    4  uint32  observation start (Unix秒。予約がなければ0)
//     42    2  uint16  observation duration (秒)
//     44    1  uint8   last observation result (ObservationResult)
//     45    4  uint32  last observation start (Unix秒。実際に始めた時刻。見送り・開始前の取り消しは予約の時刻)
//     49    2  uint16  last observation length (秒。実際に観測した長さ)
//     51    2  uint16  completed observation count
namespace HousekeepingPacket {

const uint16_t kApid = 101;
const size_t kPacketSize = 53;

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
};

// パケットをbufferに書き込み、書き込んだバイト数を返す。bufferが小さければ0を返す。
// sequenceCount は下位14bitが使われる（APIDごとに数える）。
size_t encode(const Housekeeping& hk, uint16_t sequenceCount, uint8_t* buffer, size_t size);

}  // namespace HousekeepingPacket
