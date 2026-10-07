#pragma once

#include <AckPacket.h>
#include <EventPacket.h>
#include <HousekeepingPacket.h>
#include <NmeaParser.h>
#include <WiFiUdp.h>

// 地上局との通信: Wi-Fi接続、TMの送信 (APIDごとのシーケンス番号)、TCの受信
class GroundLink {
 public:
  void begin(const char* ssid, const char* password, const char* host, uint16_t tmPort,
             uint16_t tcPort);

  bool connected() const;
  int8_t rssi() const;
  uint32_t packetsSent() const { return packetsSent_; }
  void describe(char* text, size_t size) const;

  void sendGnss(const NmeaParser& parser);
  void sendHousekeeping(const HousekeepingPacket::Housekeeping& hk);
  void sendAck(uint16_t tcSequence, uint8_t commandId, AckPacket::Stage stage, uint8_t errorCode);
  void sendEvent(const EventPacket::Event& event);

  // 届いたTCを1つ buffer に読み込む。戻り値はTCの本来の長さ (buffer より長いこともある)。なければ0
  size_t receiveTelecommand(uint8_t* buffer, size_t capacity);

 private:
  void send(const uint8_t* packet, size_t size, uint16_t& sequence);

  const char* ssid_ = nullptr;
  const char* host_ = nullptr;
  uint16_t tmPort_ = 0;
  uint16_t tcPort_ = 0;

  WiFiUDP tmUdp_;
  WiFiUDP tcUdp_;
  bool tcListening_ = false;
  uint32_t packetsSent_ = 0;

  uint16_t gnssSequence_ = 0;
  uint16_t housekeepingSequence_ = 0;
  uint16_t ackSequence_ = 0;
  uint16_t eventSequence_ = 0;
};
