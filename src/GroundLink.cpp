#include "GroundLink.h"

#include <Arduino.h>
#include <ObservationPacket.h>
#include <TelemetryPacket.h>

void GroundLink::begin(PacketTransport& transport) {
  transport_ = &transport;
  transport_->begin();
}

void GroundLink::describe(char* text, size_t size) const {
  char transport[48] = "";
  if (transport_) transport_->describe(transport, sizeof(transport));
  snprintf(text, size, "%s  tx=%lu", transport, static_cast<unsigned long>(packetsSent_));
}

void GroundLink::sendGnss(const NmeaParser& parser) {
  uint8_t packet[TelemetryPacket::kPacketSize];
  size_t size = TelemetryPacket::encode(parser.telemetry(), parser.validSentences(),
                                        parser.checksumErrors(), millis(), gnssSequence_, now(), packet,
                                        sizeof(packet));
  send(packet, size, gnssSequence_);
}

void GroundLink::sendHousekeeping(const HousekeepingPacket::Housekeeping& hk) {
  uint8_t packet[HousekeepingPacket::kPacketSize];
  size_t size = HousekeepingPacket::encode(hk, housekeepingSequence_, now(), packet, sizeof(packet));
  send(packet, size, housekeepingSequence_);
}

void GroundLink::sendAck(uint16_t tcSequence, uint8_t commandId, AckPacket::Stage stage,
                         uint8_t errorCode) {
  uint8_t packet[AckPacket::kPacketSize];
  size_t size = AckPacket::encode(tcSequence, commandId, stage, errorCode, ackSequence_, now(), packet,
                                  sizeof(packet));
  send(packet, size, ackSequence_);
}

void GroundLink::sendEvent(const EventPacket::Event& event) {
  uint8_t packet[EventPacket::kPacketSize];
  size_t size = EventPacket::encode(event, eventSequence_, now(), packet, sizeof(packet));
  send(packet, size, eventSequence_);
}

void GroundLink::sendObservation(uint32_t utcTime, const NmeaParser& parser) {
  SatelliteInfo satellites[NmeaParser::kMaxSatellites];
  size_t count = parser.currentSatellites(satellites, NmeaParser::kMaxSatellites);
  uint8_t packet[ObservationPacket::kMaxPacketSize];
  size_t size =
      ObservationPacket::encode(utcTime, satellites, count, observationSequence_, now(), packet, sizeof(packet));
  send(packet, size, observationSequence_);
}

size_t GroundLink::receiveTelecommand(uint8_t* buffer, size_t capacity) {
  return transport_ ? transport_->receive(buffer, capacity) : 0;
}

void GroundLink::send(const uint8_t* packet, size_t size, uint16_t& sequence) {
  if (size == 0 || !transport_ || !transport_->send(packet, size)) return;
  sequence = (sequence + 1) & 0x3FFF;
  packetsSent_++;
}
