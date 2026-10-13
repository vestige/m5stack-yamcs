#include "EspNowTransport.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

namespace {

const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct ReceivedPacket {
  uint8_t size;
  uint8_t data[ESP_NOW_MAX_DATA_LEN];
};

QueueHandle_t receivedQueue = nullptr;

}  // namespace

// Wi-Fiのタスクから呼ばれるので、キューに入れるだけにする
void EspNowTransport::onReceive(const uint8_t*, const uint8_t* data, int size) {
  if (size <= 0 || size > ESP_NOW_MAX_DATA_LEN) return;
  ReceivedPacket packet;
  packet.size = static_cast<uint8_t>(size);
  memcpy(packet.data, data, size);
  xQueueSend(receivedQueue, &packet, 0);
}

void EspNowTransport::begin() {
  receivedQueue = xQueueCreate(8, sizeof(ReceivedPacket));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast, sizeof(kBroadcast));
  peer.channel = channel_;
  peer.encrypt = false;
  initialized_ = esp_now_add_peer(&peer) == ESP_OK;
  Serial.printf("ESP-NOW on channel %u (%s)\n", channel_, WiFi.macAddress().c_str());
}

bool EspNowTransport::send(const uint8_t* packet, size_t size) {
  if (!initialized_ || size > ESP_NOW_MAX_DATA_LEN) return false;
  return esp_now_send(kBroadcast, packet, size) == ESP_OK;
}

size_t EspNowTransport::receive(uint8_t* buffer, size_t capacity) {
  ReceivedPacket packet;
  if (!initialized_ || xQueueReceive(receivedQueue, &packet, 0) != pdTRUE) return 0;
  memcpy(buffer, packet.data, packet.size < capacity ? packet.size : capacity);
  received_++;
  return packet.size;
}

void EspNowTransport::describe(char* text, size_t size) const {
  if (initialized_) {
    snprintf(text, size, "ESP-NOW ch%u  tc=%lu", channel_, static_cast<unsigned long>(received_));
  } else {
    snprintf(text, size, "ESP-NOW init failed");
  }
}
