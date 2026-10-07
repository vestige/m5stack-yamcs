#include "GnssReceiver.h"

// 1にすると受信したNMEAをそのままシリアルへ流す（Milestone 1の動作）
#define ECHO_RAW_NMEA 0

void GnssReceiver::begin(int rxPin, int txPin) { serial_.begin(115200, SERIAL_8N1, rxPin, txPin); }

void GnssReceiver::poll(bool payloadPower) {
  while (serial_.available()) {
    char c = serial_.read();
#if ECHO_RAW_NMEA
    Serial.write(c);
#endif
    if (payloadPower && parser_.encode(c)) lastSentenceMs_ = millis();
  }
}

bool GnssReceiver::receiving(uint32_t now) const {
  return lastSentenceMs_ != 0 && now - lastSentenceMs_ < kNoDataTimeoutMs;
}

void GnssReceiver::printTelemetry() const {
  const GnssTelemetry& t = parser_.telemetry();
  char timeText[32] = "----------T--:--:--.---Z";
  char positionText[48] = "lat=--- lon=---";
  char altitudeText[24] = "alt=---";

  if (t.timeValid && t.dateValid) {
    snprintf(timeText, sizeof(timeText), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", t.year, t.month,
             t.day, t.hour, t.minute, t.second, t.millisecond);
  } else if (t.timeValid) {
    snprintf(timeText, sizeof(timeText), "----------T%02u:%02u:%02u.%03uZ", t.hour, t.minute,
             t.second, t.millisecond);
  }
  if (t.positionValid) {
    snprintf(positionText, sizeof(positionText), "lat=%.6f lon=%.6f", t.latitude, t.longitude);
  }
  if (t.altitudeValid) {
    snprintf(altitudeText, sizeof(altitudeText), "alt=%.1fm", t.altitude);
  }

  const char* fixType = t.fixType == 3 ? "3D" : t.fixType == 2 ? "2D" : "NoFix";
  Serial.printf("[TLM] %s %s %s sats=%u fix=%s(%u) type=%s rmc=%c ok=%lu err=%lu\n", timeText,
                positionText, altitudeText, t.satellites, NmeaParser::fixQualityName(t.fixQuality),
                t.fixQuality, fixType, t.rmcActive ? 'A' : 'V',
                static_cast<unsigned long>(parser_.validSentences()),
                static_cast<unsigned long>(parser_.checksumErrors()));
}
