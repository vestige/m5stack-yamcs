#include "BusMonitor.h"

#include <Arduino.h>
#include <Preferences.h>

void BusMonitor::begin() {
  // 電源を切っても消えないよう NVS (フラッシュ) に保存する
  Preferences prefs;
  prefs.begin("m5sat", false);
  bootCount_ = prefs.getUShort("boot_count", 0) + 1;
  prefs.putUShort("boot_count", bootCount_);
  prefs.end();

  resetReason_ = static_cast<uint8_t>(esp_reset_reason());
  Serial.printf("Boot count: %u, reset reason: %u\n", bootCount_, resetReason_);
}

HousekeepingPacket::Housekeeping BusMonitor::collect(const Satellite& satellite,
                                                     const GroundLink& link) const {
  HousekeepingPacket::Housekeeping hk;
  satellite.fillHousekeeping(&hk);
  hk.wifiRssi = link.rssi();
  hk.freeHeap = ESP.getFreeHeap();
  hk.minFreeHeap = ESP.getMinFreeHeap();
  hk.uptimeMs = millis();
  hk.bootCount = bootCount_;
  hk.resetReason = resetReason_;
  return hk;
}
