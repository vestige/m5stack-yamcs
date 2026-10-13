#include "UdpTransport.h"

#include <WiFi.h>

void UdpTransport::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_, password_);
  Serial.printf("Connecting to WiFi \"%s\", TM -> %s:%u, TC <- :%u\n", ssid_, host_, tmPort_, tcPort_);
}

bool UdpTransport::ready() const { return WiFi.status() == WL_CONNECTED; }

bool UdpTransport::send(const uint8_t* packet, size_t size) {
  if (!ready()) return false;
  tmUdp_.beginPacket(host_, tmPort_);
  tmUdp_.write(packet, size);
  return tmUdp_.endPacket();
}

size_t UdpTransport::receive(uint8_t* buffer, size_t capacity) {
  if (!ready()) return 0;
  if (!tcListening_) tcListening_ = tcUdp_.begin(tcPort_);

  int size = tcUdp_.parsePacket();
  if (size <= 0) return 0;
  tcUdp_.read(buffer, capacity);
  return static_cast<size_t>(size);
}

int8_t UdpTransport::rssi() const { return ready() ? static_cast<int8_t>(WiFi.RSSI()) : 0; }

void UdpTransport::describe(char* text, size_t size) const {
  if (ready()) {
    snprintf(text, size, "UDP %s:%u  rssi=%d", host_, tmPort_, rssi());
  } else {
    snprintf(text, size, "WiFi connecting to %s ...", ssid_);
  }
}
