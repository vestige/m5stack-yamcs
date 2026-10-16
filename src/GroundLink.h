#pragma once

#include <AckPacket.h>
#include <EventPacket.h>
#include <HousekeepingPacket.h>
#include <NmeaParser.h>

#include "PacketTransport.h"

// 地上局との通信: TMの送信 (APIDごとのシーケンス番号) とTCの受信。運び方は PacketTransport に任せる
class GroundLink {
 public:
  // どの回線を使うかは起動時に決まるので、begin() で渡す
  void begin(PacketTransport& transport);
  bool connected() const { return transport_ && transport_->ready(); }
  int8_t rssi() const { return transport_ ? transport_->rssi() : 0; }
  uint32_t packetsSent() const { return packetsSent_; }
  void describe(char* text, size_t size) const;

  void sendGnss(const NmeaParser& parser);
  void sendHousekeeping(const HousekeepingPacket::Housekeeping& hk);
  void sendAck(uint16_t tcSequence, uint8_t commandId, AckPacket::Stage stage, uint8_t errorCode);
  void sendEvent(const EventPacket::Event& event);
  void sendObservation(uint32_t utcTime, const NmeaParser& parser);

  // 届いたTCを1つ buffer に読み込む。戻り値はTCの本来の長さ (buffer より長いこともある)。なければ0
  size_t receiveTelecommand(uint8_t* buffer, size_t capacity);

 private:
  void send(const uint8_t* packet, size_t size, uint16_t& sequence);

  PacketTransport* transport_ = nullptr;
  uint32_t packetsSent_ = 0;

  uint16_t gnssSequence_ = 0;
  uint16_t housekeepingSequence_ = 0;
  uint16_t ackSequence_ = 0;
  uint16_t eventSequence_ = 0;
  uint16_t observationSequence_ = 0;
};
