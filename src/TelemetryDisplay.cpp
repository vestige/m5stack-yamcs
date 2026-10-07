#include "TelemetryDisplay.h"

#include <algorithm>

namespace {

const int kWidth = 320;
const int kHeight = 240;
const int kHeaderHeight = 36;
const int kRowTop = 40;
const int kRowHeight = 28;
const int kLinkStatusY = 212;
const int kValueX = 80;

// ボタンA/B/Cの中心のx座標
const int kButtonX[] = {68, 160, 252};

const uint16_t kBackground = TFT_BLACK;
const uint16_t kLabelColor = TFT_LIGHTGREY;
const uint16_t kValueColor = TFT_WHITE;
const uint16_t kInvalidColor = TFT_DARKGREY;

// 内訳タイルに並べる衛星システム
const GnssSystem kTileSystems[] = {GnssSystem::GPS, GnssSystem::GLONASS, GnssSystem::Galileo,
                                   GnssSystem::BeiDou, GnssSystem::QZSS};
const char* const kTileNames[] = {"GPS", "GLO", "GAL", "BDS", "QZS"};
const int kTileCount = 5;

// 信号強度バー
const int kTileTop = 40;
const int kTileHeight = 40;
const int kBarTop = 86;
const int kBarRowHeight = 12;
const int kBarRows = 11;
const int kBarColumns = 2;
const int kBarLabelWidth = 26;
const int kBarMaxWidth = 100;
const int kBarMaxSnr = 50;   // バーの右端に相当する C/N0 (dB-Hz)
const int kGoodSnr = 40;     // 目安線を引く C/N0 (dB-Hz)

uint16_t systemColor(GnssSystem system) {
  switch (system) {
    case GnssSystem::GPS: return TFT_GREEN;
    case GnssSystem::GLONASS: return TFT_ORANGE;
    case GnssSystem::Galileo: return TFT_CYAN;
    case GnssSystem::BeiDou: return TFT_MAGENTA;
    case GnssSystem::QZSS: return TFT_YELLOW;
    default: return TFT_LIGHTGREY;
  }
}

}  // namespace

void TelemetryDisplay::begin() {
  M5.Display.setRotation(1);
  M5.Display.fillScreen(kBackground);

  // ちらつき防止のため画面全体を描くキャンバスを使う。
  // Core(Basic)はPSRAMが無いので8bitカラーでメモリを節約する。
  canvas_.setColorDepth(8);
  canvas_.createSprite(kWidth, kHeight);
}

void TelemetryDisplay::setSatelliteStatus(Satellite::OperationMode mode, bool payloadPower) {
  mode_ = mode;
  payloadPower_ = payloadPower;
}

void TelemetryDisplay::setLinkStatus(const char* text, bool ok) {
  snprintf(linkStatus_, sizeof(linkStatus_), "%s", text);
  linkOk_ = ok;
}

void TelemetryDisplay::draw(const NmeaParser& parser, bool receiving) {
  canvas_.fillScreen(kBackground);

  if (page_ == Page::Systems) {
    // 一覧は縦の領域をすべて使うため、ボタンのラベルは表示しない
    drawHeader("SYS", parser.telemetry(), receiving);
    drawSystemsPage(parser);
  } else if (page_ == Page::Satellites) {
    drawHeader("SATS", parser.telemetry(), receiving);
    drawSatellitesPage(parser);
    drawButtonLabels();
  } else {
    drawHeader("GNSS", parser.telemetry(), receiving);
    drawTelemetryPage(parser);
    drawButtonLabels();
  }

  canvas_.pushSprite(0, 0);
}

void TelemetryDisplay::drawTelemetryPage(const NmeaParser& parser) {
  const GnssTelemetry& t = parser.telemetry();
  char text[40];

  if (t.dateValid) {
    snprintf(text, sizeof(text), "%04u-%02u-%02u", t.year, t.month, t.day);
    drawRow(0, "DATE", text, kValueColor);
  } else {
    drawRow(0, "DATE", "----------", kInvalidColor);
  }

  if (t.timeValid) {
    snprintf(text, sizeof(text), "%02u:%02u:%02u UTC", t.hour, t.minute, t.second);
    drawRow(1, "TIME", text, kValueColor);
  } else {
    drawRow(1, "TIME", "--:--:--", kInvalidColor);
  }

  if (t.positionValid) {
    snprintf(text, sizeof(text), "%.6f %c", fabs(t.latitude), t.latitude >= 0 ? 'N' : 'S');
    drawRow(2, "LAT", text, kValueColor);
    snprintf(text, sizeof(text), "%.6f %c", fabs(t.longitude), t.longitude >= 0 ? 'E' : 'W');
    drawRow(3, "LON", text, kValueColor);
  } else {
    drawRow(2, "LAT", "---", kInvalidColor);
    drawRow(3, "LON", "---", kInvalidColor);
  }

  if (t.altitudeValid) {
    snprintf(text, sizeof(text), "%.1f m", t.altitude);
    drawRow(4, "ALT", text, kValueColor);
  } else {
    drawRow(4, "ALT", "---", kInvalidColor);
  }

  snprintf(text, sizeof(text), "%u", t.satellites);
  drawRow(5, "SATS", text, t.satellites > 0 ? kValueColor : kInvalidColor);

  // Wi-Fi / UDP の状態
  canvas_.setFont(&fonts::Font0);
  canvas_.setTextSize(1);
  canvas_.setTextDatum(top_left);
  canvas_.setTextColor(linkOk_ ? TFT_CYAN : TFT_ORANGE);
  canvas_.drawString(linkStatus_, 10, kLinkStatusY);

  // 受信したセンテンス数とチェックサムエラー数
  canvas_.setTextColor(kLabelColor);
  canvas_.setTextDatum(bottom_right);
  snprintf(text, sizeof(text), "NMEA ok=%lu err=%lu", static_cast<unsigned long>(parser.validSentences()),
           static_cast<unsigned long>(parser.checksumErrors()));
  canvas_.drawString(text, kWidth - 4, kHeight - 3);
}

void TelemetryDisplay::drawSatellitesPage(const NmeaParser& parser) {
  SatelliteInfo sats[NmeaParser::kMaxSatellites];
  size_t count = parser.currentSatellites(sats, NmeaParser::kMaxSatellites);
  char text[16];

  // 衛星システム別の内訳: 捕捉数 / 見えている数
  const int tileWidth = (kWidth - 4) / kTileCount;
  for (int i = 0; i < kTileCount; i++) {
    int inView = 0;
    int tracked = 0;
    for (size_t j = 0; j < count; j++) {
      if (sats[j].system != kTileSystems[i]) continue;
      inView++;
      if (sats[j].snr > 0) tracked++;
    }

    int x = 2 + i * tileWidth;
    uint16_t color = systemColor(kTileSystems[i]);
    canvas_.drawRoundRect(x + 1, kTileTop, tileWidth - 2, kTileHeight, 4, inView > 0 ? color : kInvalidColor);

    canvas_.setFont(&fonts::Font2);
    canvas_.setTextSize(1);
    canvas_.setTextDatum(top_center);
    canvas_.setTextColor(inView > 0 ? color : kInvalidColor);
    canvas_.drawString(kTileNames[i], x + tileWidth / 2, kTileTop + 3);

    snprintf(text, sizeof(text), "%d/%d", tracked, inView);
    canvas_.setTextColor(inView > 0 ? kValueColor : kInvalidColor);
    canvas_.drawString(text, x + tileWidth / 2, kTileTop + 21);
  }

  // 衛星ごとの信号強度バー（捕捉している衛星のみ、強い順）
  const int columnWidth = kWidth / kBarColumns;
  const int capacity = kBarRows * kBarColumns;
  int shown = 0;
  int tracked = 0;

  canvas_.setFont(&fonts::Font0);
  canvas_.setTextSize(1);
  for (size_t i = 0; i < count; i++) {
    const SatelliteInfo& sat = sats[i];
    if (sat.snr == 0) continue;
    tracked++;
    if (shown >= capacity) continue;

    int column = shown / kBarRows;
    int row = shown % kBarRows;
    int x = 4 + column * columnWidth;
    int y = kBarTop + row * kBarRowHeight;
    uint16_t color = systemColor(sat.system);

    snprintf(text, sizeof(text), "%c%02u", NmeaParser::systemLetter(sat.system), sat.prn);
    canvas_.setTextDatum(top_left);
    canvas_.setTextColor(color);
    canvas_.drawString(text, x, y + 2);

    int barX = x + kBarLabelWidth;
    int barWidth = kBarMaxWidth * (sat.snr > kBarMaxSnr ? kBarMaxSnr : sat.snr) / kBarMaxSnr;
    canvas_.fillRect(barX, y + 1, barWidth, kBarRowHeight - 3, color);

    snprintf(text, sizeof(text), "%u", sat.snr);
    canvas_.setTextColor(kValueColor);
    canvas_.drawString(text, barX + kBarMaxWidth + 4, y + 2);
    shown++;
  }

  // 良好な信号の目安線 (40 dB-Hz)
  for (int column = 0; column < kBarColumns; column++) {
    int x = 4 + column * columnWidth + kBarLabelWidth + kBarMaxWidth * kGoodSnr / kBarMaxSnr;
    for (int y = kBarTop; y < kBarTop + kBarRows * kBarRowHeight; y += 4) {
      canvas_.drawFastVLine(x, y, 2, kInvalidColor);
    }
  }

  canvas_.setTextDatum(bottom_right);
  canvas_.setTextColor(kLabelColor);
  if (tracked == 0) {
    canvas_.setTextDatum(middle_center);
    canvas_.drawString("No signal", kWidth / 2, kBarTop + kBarRows * kBarRowHeight / 2);
  } else if (tracked > shown) {
    snprintf(text, sizeof(text), "+%d more", tracked - shown);
    canvas_.drawString(text, kWidth - 4, kHeight - 3);
  }
}

void TelemetryDisplay::drawSystemsPage(const NmeaParser& parser) {
  // 衛星システムごとの名前と運用国
  struct SystemLabel {
    GnssSystem system;
    const char* name;
    const char* country;
  };
  const SystemLabel kSystems[] = {
      {GnssSystem::GPS, "GPS", "アメリカ"},
      {GnssSystem::GLONASS, "GLONASS", "ロシア"},
      {GnssSystem::Galileo, "Galileo", "欧州 (EU)"},
      {GnssSystem::BeiDou, "BeiDou", "中国"},
      {GnssSystem::QZSS, "QZSS みちびき", "日本"},
  };
  const int kSectionTop = 40;
  const int kSectionHeight = 40;
  const int kChipLeft = 14;
  const int kChipPitch = 20;
  const int kMaxChips = (kWidth - kChipLeft - 24) / kChipPitch;

  SatelliteInfo sats[NmeaParser::kMaxSatellites];
  size_t count = parser.currentSatellites(sats, NmeaParser::kMaxSatellites);
  // この画面では衛星番号の順に並べる
  std::sort(sats, sats + count,
            [](const SatelliteInfo& a, const SatelliteInfo& b) { return a.prn < b.prn; });

  char text[24];
  for (int i = 0; i < 5; i++) {
    const SystemLabel& label = kSystems[i];
    uint16_t color = systemColor(label.system);
    int y = kSectionTop + i * kSectionHeight;

    int inView = 0;
    int tracked = 0;
    for (size_t j = 0; j < count; j++) {
      if (sats[j].system != label.system) continue;
      inView++;
      if (sats[j].snr > 0) tracked++;
    }

    canvas_.fillRect(4, y + 3, 4, kSectionHeight - 6, inView > 0 ? color : kInvalidColor);

    // 1行目: システム名・国・捕捉数/見えている数
    canvas_.setFont(&fonts::lgfxJapanGothic_16);
    canvas_.setTextSize(1);
    canvas_.setTextDatum(top_left);
    canvas_.setTextColor(inView > 0 ? color : kInvalidColor);
    canvas_.drawString(label.name, kChipLeft, y + 3);
    canvas_.setTextColor(inView > 0 ? kValueColor : kInvalidColor);
    canvas_.drawString(label.country, 136, y + 3);
    snprintf(text, sizeof(text), "%d/%d機", tracked, inView);
    canvas_.setTextDatum(top_right);
    canvas_.drawString(text, kWidth - 6, y + 3);

    // 2行目: 衛星番号。信号を捕捉している衛星はシステムの色、捕捉していない衛星は灰色
    canvas_.setFont(&fonts::Font0);
    canvas_.setTextDatum(top_left);
    if (inView == 0) {
      canvas_.setTextColor(kInvalidColor);
      canvas_.drawString("not in view", kChipLeft, y + 24);
      continue;
    }
    int shown = 0;
    for (size_t j = 0; j < count; j++) {
      const SatelliteInfo& sat = sats[j];
      if (sat.system != label.system) continue;
      if (shown >= kMaxChips) break;
      snprintf(text, sizeof(text), "%02u", sat.prn);
      canvas_.setTextColor(sat.snr > 0 ? color : kInvalidColor);
      canvas_.drawString(text, kChipLeft + shown * kChipPitch, y + 24);
      shown++;
    }
    if (inView > shown) {
      snprintf(text, sizeof(text), "+%d", inView - shown);
      canvas_.setTextColor(kLabelColor);
      canvas_.setTextDatum(top_right);
      canvas_.drawString(text, kWidth - 6, y + 24);
    }
  }
}

void TelemetryDisplay::drawHeader(const char* title, const GnssTelemetry& t, bool receiving) {
  canvas_.fillRect(0, 0, kWidth, kHeaderHeight, TFT_NAVY);

  canvas_.setFont(&fonts::Font4);
  canvas_.setTextSize(1);
  canvas_.setTextColor(TFT_WHITE);
  canvas_.setTextDatum(middle_left);
  canvas_.drawString(title, 8, kHeaderHeight / 2);

  // 運用モード
  uint16_t modeColor = mode_ == Satellite::OperationMode::Safe      ? TFT_RED
                       : mode_ == Satellite::OperationMode::Mission ? TFT_CYAN
                                                                    : TFT_WHITE;
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextColor(modeColor);
  canvas_.drawString(Satellite::modeName(mode_), 76, kHeaderHeight / 2);

  // ペイロードの状態と Fix status をバッジで表示する
  const char* badge;
  uint16_t badgeColor;
  uint16_t badgeTextColor = TFT_BLACK;
  if (!payloadPower_) {
    badge = "PAYLOAD OFF";
    badgeColor = TFT_DARKGREY;
    badgeTextColor = TFT_WHITE;
    receiving = false;  // 測位品質も表示しない
  } else if (!receiving) {
    badge = "NO DATA";
    badgeColor = TFT_DARKGREY;
    badgeTextColor = TFT_WHITE;
  } else if (t.fixQuality == 0) {
    badge = "NO FIX";
    badgeColor = TFT_RED;
    badgeTextColor = TFT_WHITE;
  } else if (t.fixType == 3) {
    badge = "3D FIX";
    badgeColor = TFT_GREEN;
  } else {
    badge = "2D FIX";
    badgeColor = TFT_YELLOW;
  }

  const int badgeW = 120;
  const int badgeX = kWidth - badgeW - 6;
  canvas_.fillRoundRect(badgeX, 5, badgeW, kHeaderHeight - 10, 6, badgeColor);
  canvas_.setFont(&fonts::Font2);
  canvas_.setTextColor(badgeTextColor);
  canvas_.setTextDatum(middle_center);
  canvas_.drawString(badge, badgeX + badgeW / 2, kHeaderHeight / 2);

  // 測位品質(GGA quality)をバッジの左に表示する
  if (receiving) {
    canvas_.setTextColor(kLabelColor);
    canvas_.setTextDatum(middle_right);
    canvas_.drawString(NmeaParser::fixQualityName(t.fixQuality), badgeX - 8, kHeaderHeight / 2);
  }
}

void TelemetryDisplay::drawButtonLabels() {
  const char* labels[] = {"INFO", "SATS", "SYS"};
  const Page pages[] = {Page::Telemetry, Page::Satellites, Page::Systems};

  canvas_.setFont(&fonts::Font0);
  canvas_.setTextSize(1);
  canvas_.setTextDatum(bottom_center);
  for (int i = 0; i < 3; i++) {
    bool active = page_ == pages[i];
    if (active) canvas_.fillRect(kButtonX[i] - 20, kHeight - 2, 40, 2, TFT_WHITE);
    canvas_.setTextColor(active ? TFT_WHITE : kInvalidColor);
    canvas_.drawString(labels[i], kButtonX[i], kHeight - 3);
  }
}

void TelemetryDisplay::drawRow(int index, const char* label, const char* value, uint16_t color) {
  int y = kRowTop + index * kRowHeight + kRowHeight / 2;

  canvas_.setFont(&fonts::Font2);
  canvas_.setTextSize(1);
  canvas_.setTextColor(kLabelColor);
  canvas_.setTextDatum(middle_left);
  canvas_.drawString(label, 10, y);

  canvas_.setFont(&fonts::Font4);
  canvas_.setTextColor(color);
  canvas_.drawString(value, kValueX, y);
}
