#pragma once

#include <stddef.h>
#include <stdint.h>

// 地上局とパケットをやり取りする回線 (Wi-Fi の UDP、ESP-NOW など)
class PacketTransport {
 public:
  virtual ~PacketTransport() = default;

  virtual void begin() = 0;
  // パケットを送れる状態か
  virtual bool ready() const = 0;
  virtual bool send(const uint8_t* packet, size_t size) = 0;
  // 届いたパケットを1つ buffer に読み込む。戻り値は本来の長さ (buffer より長いこともある)。なければ0
  virtual size_t receive(uint8_t* buffer, size_t capacity) = 0;
  // 電波の強さ (dBm)。わからなければ0
  virtual int8_t rssi() const { return 0; }
  virtual void describe(char* text, size_t size) const = 0;
};
