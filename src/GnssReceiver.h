#pragma once

#include <Arduino.h>
#include <NmeaParser.h>

// ペイロード: Unit GPS SMA (AT6668) から NMEA を受信する
class GnssReceiver {
 public:
  void begin(int rxPin, int txPin);
  // 届いたNMEAを読む。ペイロードの電源がOFFの間は読み捨てる
  void poll(bool payloadPower);

  const NmeaParser& parser() const { return parser_; }
  bool receiving(uint32_t now) const;
  void printTelemetry() const;

 private:
  static const uint32_t kNoDataTimeoutMs = 3000;

  HardwareSerial serial_{2};
  NmeaParser parser_;
  uint32_t lastSentenceMs_ = 0;
};
