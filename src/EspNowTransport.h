#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "PacketTransport.h"

// アクセスポイントを使わず、地上局の無線機 (src/ground/) と ESP-NOW で直接やり取りする。
// TM はブロードキャストで送り、TC はどこから届いても受け付ける。
class EspNowTransport : public PacketTransport {
 public:
  explicit EspNowTransport(uint8_t channel) : channel_(channel) {}

  void begin() override;
  bool ready() const override { return initialized_; }
  bool send(const uint8_t* packet, size_t size) override;
  size_t receive(uint8_t* buffer, size_t capacity) override;
  void describe(char* text, size_t size) const override;

 private:
  static void onReceive(const uint8_t* mac, const uint8_t* data, int size);

  uint8_t channel_;
  bool initialized_ = false;
  uint32_t received_ = 0;
};
