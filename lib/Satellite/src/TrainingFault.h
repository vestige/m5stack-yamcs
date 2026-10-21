#pragma once

#include <stddef.h>
#include <stdint.h>

// 訓練用の異常 (5-10)。出題者 (tools/inject_fault.py) が地上局から送る APID 111 のパケットで起こす。
// 本物の運用では、訓練の異常は地上のシミュレータに入れ、打ち上げる衛星には入れない。
// M5Sat には別のシミュレータがないので、衛星のプログラムがシミュレータの役も兼ねる。
// 運用者に見えるのは異常の「症状」だけにするため、このパケットには ACK を返さず、コマンドの数にも数えない。
//
// offset size  field
//      0    6  CCSDS primary header (TC, APID=111)
//      6    1  uint8   kind (Kind)
//      7    2  uint16  duration (秒。Reboot と Clear では使わない)
namespace TrainingFault {

const uint16_t kApid = 111;
const size_t kPacketSize = 9;

enum class Kind : uint8_t {
  Clear = 0,        // 続いている異常をすべて止める
  GnssStop = 1,     // GPS のデータを受け取れなくなる
  CommandLoss = 2,  // コマンドを受け付けなくなる (ACK も返さない)
  Reboot = 3,       // 予告なく再起動する
};

struct Request {
  Kind kind = Kind::Clear;
  uint16_t durationSec = 0;
};

// APID 111 の TC パケットなら true (中身が正しいかは parse で確かめる)
bool isTrainingPacket(const uint8_t* data, size_t size);
// 正しい訓練用のパケットなら true を返し、request に入れる
bool parse(const uint8_t* data, size_t size, Request* request);
// 地上 (tools/inject_fault.py) と同じ形でパケットを作る。テスト用
size_t encode(const Request& request, uint16_t sequenceCount, uint8_t* buffer, size_t size);

}  // namespace TrainingFault

// 起きている訓練用の異常と、いつまで続くか
class TrainingFaults {
 public:
  void apply(const TrainingFault::Request& request, uint32_t nowMs);

  bool gnssStopped(uint32_t nowMs) const { return active(gnssStop_, gnssStopUntilMs_, nowMs); }
  bool commandsIgnored(uint32_t nowMs) const { return active(commandLoss_, commandLossUntilMs_, nowMs); }
  // 再起動を求められていれば true を返す。1回だけ
  bool takeRebootRequest();

 private:
  static bool active(bool on, uint32_t untilMs, uint32_t nowMs) {
    return on && static_cast<int32_t>(untilMs - nowMs) > 0;
  }

  bool gnssStop_ = false;
  uint32_t gnssStopUntilMs_ = 0;
  bool commandLoss_ = false;
  uint32_t commandLossUntilMs_ = 0;
  bool rebootRequested_ = false;
};
