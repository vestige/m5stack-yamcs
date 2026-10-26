#pragma once

#include <M5Unified.h>
#include <NmeaParser.h>
#include <Satellite.h>

// M5Stackの画面(320x240)にGNSSテレメトリを表示する
class TelemetryDisplay {
 public:
  enum class Page { Telemetry, Satellites, Systems };

  void begin();
  // ボタンA/B/Cでページを切り替える。切り替わったら true
  bool handleButtons();

  // INFO画面に出す通信状態 (ok=falseなら注意色で表示する)
  void setLinkStatus(const char* text, bool ok);
  // ヘッダに出す衛星の状態
  void setSatelliteStatus(Satellite::OperationMode mode, bool payloadPower);
  // ミッション要求のランプ (SET_LAMP)。点いている間、どのページでも右下に色の丸を出す
  void setLamp(Satellite::LampColor color, uint16_t remainingSec);

  // receiving: 直近でNMEAを受信できているか
  void draw(const NmeaParser& parser, bool receiving);

 private:
  void drawTelemetryPage(const NmeaParser& parser);
  void drawSatellitesPage(const NmeaParser& parser);
  void drawSystemsPage(const NmeaParser& parser);
  bool drawPayloadOffNotice();
  void drawHeader(const char* title, const GnssTelemetry& t, bool receiving);
  void drawButtonLabels();
  void drawRow(int index, const char* label, const char* value, uint16_t color);
  void drawLamp();

  M5Canvas canvas_{&M5.Display};
  Page page_ = Page::Telemetry;
  char linkStatus_[64] = "";
  bool linkOk_ = false;
  Satellite::OperationMode mode_ = Satellite::OperationMode::Nominal;
  bool payloadPower_ = true;
  Satellite::LampColor lampColor_ = Satellite::LampColor::Off;
  uint16_t lampRemainingSec_ = 0;
};
