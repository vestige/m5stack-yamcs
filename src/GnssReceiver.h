#pragma once

#include <Arduino.h>
#include <NmeaParser.h>

// ペイロード: Unit GPS SMA (AT6668) から NMEA を受信する
class GnssReceiver {
 public:
  void begin(int rxPin, int txPin);
  // 届いたNMEAを読む。ペイロードの電源がOFFの間は読み捨てる。
  // 電源を切ったら、受信の状態 (位置や衛星の一覧) も消える
  void poll(bool payloadPower);

  const NmeaParser& parser() const { return parser_; }
  bool receiving(uint32_t now) const;
  // NMEAが最後に届いた時刻 (まだ届いていなければ0)
  uint32_t lastSentenceMs() const { return lastSentenceMs_; }
  // GPSから新しいUTC時刻 (Unix秒) が届いていれば true を返し、utcSec に入れる。届いた時刻ごとに1回だけ
  bool takeNewUtc(uint32_t* utcSec);
  void printTelemetry() const;

 private:
  static const uint32_t kNoDataTimeoutMs = 3000;

  HardwareSerial serial_{2};
  NmeaParser parser_;
  uint32_t lastSentenceMs_ = 0;
  bool powered_ = true;
  uint32_t lastUtc_ = 0;
};
