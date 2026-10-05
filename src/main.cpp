#include <Arduino.h>
#include <M5Unified.h>
#include <NmeaParser.h>
#include <TelemetryPacket.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "TelemetryDisplay.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h がありません。include/secrets.example.h をコピーしてWi-Fiと送信先を設定してください"
#endif

// M5Stack Core の Grove Port A
// GPS Unit: TX → GPIO22 (M5Stack RX)
//           RX → GPIO21 (M5Stack TX)
static const int GPS_RX = 22;
static const int GPS_TX = 21;

// 1にすると受信したNMEAをそのままシリアルへ流す（Milestone 1の動作）
#define ECHO_RAW_NMEA 0

static const uint32_t TELEMETRY_INTERVAL_MS = 1000;
// この時間NMEAを受信できなければ画面に NO DATA を表示する
static const uint32_t NO_DATA_TIMEOUT_MS = 3000;

HardwareSerial GPS(2);
NmeaParser parser;
TelemetryDisplay display;
WiFiUDP udp;
uint32_t lastReportMs = 0;
uint32_t lastSentenceMs = 0;
uint16_t packetSequence = 0;
uint32_t packetsSent = 0;

// テレメトリをCCSDS Space PacketにしてUDPで送る。Wi-Fi未接続なら送らない。
void sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return;

  uint8_t packet[TelemetryPacket::kPacketSize];
  size_t size = TelemetryPacket::encode(parser.telemetry(), parser.validSentences(),
                                        parser.checksumErrors(), millis(), packetSequence, packet,
                                        sizeof(packet));
  udp.beginPacket(TELEMETRY_HOST, TELEMETRY_PORT);
  udp.write(packet, size);
  if (udp.endPacket()) {
    packetSequence = (packetSequence + 1) & 0x3FFF;
    packetsSent++;
  }
}

// 画面に出すWi-Fi/UDPの状態
void updateLinkStatus() {
  char text[64];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(text, sizeof(text), "UDP %s:%d  tx=%lu", TELEMETRY_HOST, TELEMETRY_PORT,
             static_cast<unsigned long>(packetsSent));
    display.setLinkStatus(text, true);
  } else {
    snprintf(text, sizeof(text), "WiFi connecting to %s ...", WIFI_SSID);
    display.setLinkStatus(text, false);
  }
}

void printTelemetry(const GnssTelemetry& t) {
  char line[192];
  char timeText[32] = "----------T--:--:--.---Z";
  char positionText[48] = "lat=--- lon=---";
  char altitudeText[24] = "alt=---";

  if (t.timeValid) {
    if (t.dateValid) {
      snprintf(timeText, sizeof(timeText), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.year, t.month,
               t.day, t.hour, t.minute, t.second, t.millisecond);
    } else {
      snprintf(timeText, sizeof(timeText), "----------T%02u:%02u:%02u.%03uZ", t.hour, t.minute,
               t.second, t.millisecond);
    }
  }
  if (t.positionValid) {
    snprintf(positionText, sizeof(positionText), "lat=%.6f lon=%.6f", t.latitude, t.longitude);
  }
  if (t.altitudeValid) {
    snprintf(altitudeText, sizeof(altitudeText), "alt=%.1fm", t.altitude);
  }

  const char* fixType = t.fixType == 3 ? "3D" : t.fixType == 2 ? "2D" : "NoFix";
  snprintf(line, sizeof(line), "[TLM] %s %s %s sats=%u fix=%s(%u) type=%s rmc=%c ok=%lu err=%lu",
           timeText, positionText, altitudeText, t.satellites, NmeaParser::fixQualityName(t.fixQuality),
           t.fixQuality, fixType, t.rmcActive ? 'A' : 'V',
           static_cast<unsigned long>(parser.validSentences()),
           static_cast<unsigned long>(parser.checksumErrors()));
  Serial.println(line);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  delay(1000);

  Serial.println();
  Serial.println("====================");
  Serial.println("M5Stack GNSS telemetry");
  Serial.println("====================");

  // Core(Basic)のGrove Port A(GPIO21/22)は内部I2Cと共用のため、
  // M5Unifiedが確保したI2Cを解放してからGPSのUARTに割り当てる
  M5.In_I2C.release();

  // Unit GPS SMA (AT6668)
  GPS.begin(115200, SERIAL_8N1, GPS_RX, GPS_TX);

  display.begin();

  // 接続はバックグラウンドで進み、切断されても自動で再接続する
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi \"%s\", telemetry -> %s:%d\n", WIFI_SSID, TELEMETRY_HOST,
                TELEMETRY_PORT);

  Serial.println("Waiting for GPS data...");
}

void loop() {
  while (GPS.available()) {
    char c = GPS.read();
#if ECHO_RAW_NMEA
    Serial.write(c);
#endif
    if (parser.encode(c)) lastSentenceMs = millis();
  }

  // ボタンA: テレメトリ画面 / ボタンB: 衛星画面 / ボタンC: 衛星システム一覧
  M5.update();
  bool pageChanged = false;
  if (M5.BtnA.wasPressed() && display.page() != TelemetryDisplay::Page::Telemetry) {
    display.setPage(TelemetryDisplay::Page::Telemetry);
    pageChanged = true;
  }
  if (M5.BtnB.wasPressed() && display.page() != TelemetryDisplay::Page::Satellites) {
    display.setPage(TelemetryDisplay::Page::Satellites);
    pageChanged = true;
  }
  if (M5.BtnC.wasPressed() && display.page() != TelemetryDisplay::Page::Systems) {
    display.setPage(TelemetryDisplay::Page::Systems);
    pageChanged = true;
  }

  uint32_t now = millis();
  bool receiving = lastSentenceMs != 0 && now - lastSentenceMs < NO_DATA_TIMEOUT_MS;
  if (now - lastReportMs >= TELEMETRY_INTERVAL_MS) {
    lastReportMs = now;
    printTelemetry(parser.telemetry());
    sendTelemetry();
    updateLinkStatus();
    display.draw(parser, receiving);
  } else if (pageChanged) {
    // ページ切り替えは次の更新を待たずにすぐ描画する
    display.draw(parser, receiving);
  }
}
