#include <Arduino.h>
#include <M5Unified.h>
#include <Satellite.h>

#include "BusMonitor.h"
#include "CommandHandler.h"
#include "EspNowTransport.h"
#include "GnssReceiver.h"
#include "GroundLink.h"
#include "IntervalTimer.h"
#include "LinkSetting.h"
#include "PayloadLed.h"
#include "TelemetryDisplay.h"
#include "UdpTransport.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h がありません。include/secrets.example.h をコピーしてWi-Fiと送信先を設定してください"
#endif

#ifndef TELECOMMAND_PORT
#define TELECOMMAND_PORT 10025
#endif
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif
// NVSにまだ回線が保存されていないときに使う回線
#ifndef GROUND_LINK_ESPNOW
#define GROUND_LINK_ESPNOW 0
#endif

// Grove Port A: GPS Unit の TX → GPIO22, RX → GPIO21
static const int GPS_RX = 22;
static const int GPS_TX = 21;
// G25はスピーカー、G16/G17は将来GPSをPort Cへ移すときのために空けておく
static const int PAYLOAD_LED_PIN = 26;

static const uint32_t DISPLAY_INTERVAL_MS = 1000;
static const uint32_t LINK_SWITCH_HOLD_MS = 2000;

Satellite satellite;
GnssReceiver gnss;
UdpTransport udpTransport(WIFI_SSID, WIFI_PASSWORD, TELEMETRY_HOST, TELEMETRY_PORT, TELECOMMAND_PORT);
EspNowTransport espNowTransport(ESPNOW_CHANNEL);
LinkSetting::Link activeLink;
GroundLink groundLink;
CommandHandler commands(satellite, groundLink);
BusMonitor bus;
TelemetryDisplay display;
PayloadLed payloadLed(PAYLOAD_LED_PIN);

IntervalTimer telemetryTimer;
IntervalTimer displayTimer;

void sendTelemetry() {
  if (satellite.payloadPower()) groundLink.sendGnss(gnss.parser());
  groundLink.sendHousekeeping(bus.collect(satellite, groundLink));
}

// 地上局とつながるまでのイベントは衛星に溜めておく
void sendEvents() {
  EventPacket::Event event;
  while (groundLink.connected() && satellite.popEvent(&event)) groundLink.sendEvent(event);
}

void reportBoot() {
  EventPacket::Event event;
  event.id = EventPacket::EventId::Boot;
  event.arg1 = bus.bootCount();
  event.arg2 = bus.resetReason();
  satellite.report(event);
}

// 右ボタンの長押しで回線を切り替える。回線は起動時に決めるので、保存して再起動する
void switchLinkOnLongPress() {
  if (!M5.BtnC.wasHold()) return;
  LinkSetting::Link next = LinkSetting::other(activeLink);
  LinkSetting::save(next);
  Serial.printf("Switching ground link to %s\n", LinkSetting::name(next));

  M5.Display.fillScreen(TFT_NAVY);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.drawString("Switching link to", M5.Display.width() / 2, M5.Display.height() / 2 - 16);
  M5.Display.drawString(LinkSetting::name(next), M5.Display.width() / 2, M5.Display.height() / 2 + 16);
  delay(1500);
  ESP.restart();
}

void refreshDisplay(uint32_t now) {
  char linkStatus[64];
  groundLink.describe(linkStatus, sizeof(linkStatus));
  display.setLinkStatus(linkStatus, groundLink.connected());
  display.setSatelliteStatus(satellite.mode(), satellite.payloadPower());
  display.draw(gnss.parser(), gnss.receiving(now));
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  M5.Speaker.setVolume(64);
  Serial.println("\nM5Sat (M5Stack GNSS telemetry)");

  // Core(Basic) の Port A は内部I2Cとピンを共用しているので、I2Cを解放してからGPSに使う
  M5.In_I2C.release();
  gnss.begin(GPS_RX, GPS_TX);

  display.begin();
  payloadLed.begin(satellite.payloadPower());
  bus.begin();
  reportBoot();
  activeLink = LinkSetting::load(GROUND_LINK_ESPNOW ? LinkSetting::Link::EspNow : LinkSetting::Link::Udp);
  Serial.printf("Ground link: %s\n", LinkSetting::name(activeLink));
  if (activeLink == LinkSetting::Link::EspNow) {
    groundLink.begin(espNowTransport);
  } else {
    groundLink.begin(udpTransport);
  }
  M5.BtnC.setHoldThresh(LINK_SWITCH_HOLD_MS);
}

void loop() {
  gnss.poll(satellite.payloadPower());
  commands.poll();
  satellite.checkHealth(millis(), gnss.lastSentenceMs());
  sendEvents();
  payloadLed.show(satellite.payloadPower());

  M5.update();
  switchLinkOnLongPress();
  bool pageChanged = display.handleButtons();

  uint32_t now = millis();
  if (telemetryTimer.elapsed(now, satellite.tmIntervalMs())) sendTelemetry();
  if (displayTimer.elapsed(now, DISPLAY_INTERVAL_MS)) {
    gnss.printTelemetry();
    refreshDisplay(now);
  } else if (pageChanged) {
    refreshDisplay(now);
  }
}
