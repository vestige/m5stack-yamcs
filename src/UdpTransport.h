#pragma once

#include <WiFiUdp.h>

#include "PacketTransport.h"

// Wi-Fiのアクセスポイントにつなぎ、地上局 (Mac) と UDP でやり取りする
class UdpTransport : public PacketTransport {
 public:
  UdpTransport(const char* ssid, const char* password, const char* host, uint16_t tmPort, uint16_t tcPort)
      : ssid_(ssid), password_(password), host_(host), tmPort_(tmPort), tcPort_(tcPort) {}

  void begin() override;
  bool ready() const override;
  bool send(const uint8_t* packet, size_t size) override;
  size_t receive(uint8_t* buffer, size_t capacity) override;
  int8_t rssi() const override;
  void describe(char* text, size_t size) const override;

 private:
  const char* ssid_;
  const char* password_;
  const char* host_;
  uint16_t tmPort_;
  uint16_t tcPort_;

  WiFiUDP tmUdp_;
  WiFiUDP tcUdp_;
  bool tcListening_ = false;
};
