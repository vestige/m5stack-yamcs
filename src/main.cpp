#include <Arduino.h>
#include <M5Unified.h>
#include <Satellite.h>

#include "BusMonitor.h"
#include "CommandHandler.h"
#include "GnssReceiver.h"
#include "GroundLink.h"
#include "IntervalTimer.h"
#include "PayloadLed.h"
#include "TelemetryDisplay.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h がありません。include/secrets.example.h をコピーしてWi-Fiと送信先を設定してください"
#endif

#ifndef TELECOMMAND_PORT
#define TELECOMMAND_PORT 10025
#endif

// Grove Port A: GPS Unit の TX → GPIO22, RX → GPIO21
static const int GPS_RX = 22;
static const int GPS_TX = 21;
// G25はスピーカー、G16/G17は将来GPSをPort Cへ移すときのために空けておく
static const int PAYLOAD_LED_PIN = 26;

static const uint32_t DISPLAY_INTERVAL_MS = 1000;

Satellite satellite;
GnssReceiver gnss;
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
  groundLink.begin(WIFI_SSID, WIFI_PASSWORD, TELEMETRY_HOST, TELEMETRY_PORT, TELECOMMAND_PORT);
}

void loop() {
  gnss.poll(satellite.payloadPower());
  commands.poll();
  payloadLed.show(satellite.payloadPower());

  M5.update();
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
