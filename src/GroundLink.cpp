#include "GroundLink.h"

#include <TelemetryPacket.h>
#include <WiFi.h>

void GroundLink::begin(const char* ssid, const char* password, const char* host, uint16_t tmPort,
                       uint16_t tcPort) {
  ssid_ = ssid;
  host_ = host;
  tmPort_ = tmPort;
  tcPort_ = tcPort;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to WiFi \"%s\", TM -> %s:%u, TC <- :%u\n", ssid, host, tmPort, tcPort);
}

bool GroundLink::connected() const { return WiFi.status() == WL_CONNECTED; }

int8_t GroundLink::rssi() const { return connected() ? static_cast<int8_t>(WiFi.RSSI()) : 0; }

void GroundLink::describe(char* text, size_t size) const {
  if (connected()) {
    snprintf(text, size, "UDP %s:%u  tx=%lu  rssi=%d", host_, tmPort_,
             static_cast<unsigned long>(packetsSent_), rssi());
  } else {
    snprintf(text, size, "WiFi connecting to %s ...", ssid_);
  }
}

void GroundLink::sendGnss(const NmeaParser& parser) {
  uint8_t packet[TelemetryPacket::kPacketSize];
  size_t size = TelemetryPacket::encode(parser.telemetry(), parser.validSentences(),
                                        parser.checksumErrors(), millis(), gnssSequence_, packet,
                                        sizeof(packet));
  send(packet, size, gnssSequence_);
}

void GroundLink::sendHousekeeping(const HousekeepingPacket::Housekeeping& hk) {
  uint8_t packet[HousekeepingPacket::kPacketSize];
  size_t size = HousekeepingPacket::encode(hk, housekeepingSequence_, packet, sizeof(packet));
  send(packet, size, housekeepingSequence_);
}

void GroundLink::sendAck(uint16_t tcSequence, uint8_t commandId, AckPacket::Stage stage,
                         uint8_t errorCode) {
  uint8_t packet[AckPacket::kPacketSize];
  size_t size = AckPacket::encode(tcSequence, commandId, stage, errorCode, ackSequence_, packet,
                                  sizeof(packet));
  send(packet, size, ackSequence_);
}

size_t GroundLink::receiveTelecommand(uint8_t* buffer, size_t capacity) {
  if (!connected()) return 0;
  if (!tcListening_) tcListening_ = tcUdp_.begin(tcPort_);

  int size = tcUdp_.parsePacket();
  if (size <= 0) return 0;
  tcUdp_.read(buffer, capacity);
  return static_cast<size_t>(size);
}

void GroundLink::send(const uint8_t* packet, size_t size, uint16_t& sequence) {
  if (!connected() || size == 0) return;

  tmUdp_.beginPacket(host_, tmPort_);
  tmUdp_.write(packet, size);
  if (tmUdp_.endPacket()) {
    sequence = (sequence + 1) & 0x3FFF;
    packetsSent_++;
  }
}
