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
namespace HousekeepingPacket {

const uint16_t kApid = 101;
const size_t kPacketSize = 33;

enum class OperationMode : uint8_t { Safe = 0, Nominal = 1, Mission = 2 };

// FDIRが検知した異常
enum class Fault : uint8_t { None = 0, GnssNoData = 1 };

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
};

// パケットをbufferに書き込み、書き込んだバイト数を返す。bufferが小さければ0を返す。
// sequenceCount は下位14bitが使われる（APIDごとに数える）。
size_t encode(const Housekeeping& hk, uint16_t sequenceCount, uint8_t* buffer, size_t size);

}  // namespace HousekeepingPacket
