#pragma once

#include <M5Unified.h>
#include <NmeaParser.h>

// M5Stackの画面(320x240)にGNSSテレメトリを表示する
class TelemetryDisplay {
 public:
  enum class Page { Telemetry, Satellites, Systems };

  void begin();
  void setPage(Page page) { page_ = page; }
  Page page() const { return page_; }

  // receiving: 直近でNMEAを受信できているか
  void draw(const NmeaParser& parser, bool receiving);

 private:
  void drawTelemetryPage(const NmeaParser& parser);
  void drawSatellitesPage(const NmeaParser& parser);
  void drawSystemsPage(const NmeaParser& parser);
  void drawHeader(const char* title, const GnssTelemetry& t, bool receiving);
  void drawButtonLabels();
  void drawRow(int index, const char* label, const char* value, uint16_t color);

  M5Canvas canvas_{&M5.Display};
  Page page_ = Page::Telemetry;
};
