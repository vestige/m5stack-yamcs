#pragma once

#include <stddef.h>
#include <stdint.h>

// NMEAセンテンスから抽出したGNSSテレメトリ
struct GnssTelemetry {
  // UTC時刻（GGA / RMC）
  bool timeValid = false;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint16_t millisecond = 0;

  // UTC日付（RMC）
  bool dateValid = false;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;

  // 位置（GGA）: 度単位、北緯・東経が正
  bool positionValid = false;
  double latitude = 0.0;
  double longitude = 0.0;

  // 海抜高度（GGA）: メートル
  bool altitudeValid = false;
  float altitude = 0.0f;

  // 測位に使用している衛星数（GGA）
  uint8_t satellites = 0;

  // Fix status
  uint8_t fixQuality = 0;  // GGA: 0=invalid, 1=GPS, 2=DGPS, 4=RTK fixed, 5=RTK float, 6=estimated
  uint8_t fixType = 1;     // GSA: 1=no fix, 2=2D, 3=3D
  bool rmcActive = false;  // RMC: A=active, V=void
};

// 衛星システム（GSVのTalker IDから判定する）
enum class GnssSystem : uint8_t { GPS, GLONASS, Galileo, BeiDou, QZSS, NavIC, Unknown };

// GSVから得た衛星1機分の情報
struct SatelliteInfo {
  GnssSystem system = GnssSystem::Unknown;
  uint8_t prn = 0;         // 衛星番号。0は空きスロット
  int8_t elevation = -1;   // 仰角(度)。-1は不明
  int16_t azimuth = -1;    // 方位角(度)。-1は不明
  uint8_t snr = 0;         // 信号強度 C/N0 (dB-Hz)。0は信号を捕捉していない
  uint32_t epoch = 0;      // 最後に更新されたエポック
};

// 1文字ずつ入力してNMEAセンテンスを解析するパーサ。
// Arduinoに依存しないため、native環境でユニットテストできる。
class NmeaParser {
 public:
  // 1文字入力する。チェックサムが正しいセンテンスを処理し終えたらtrueを返す。
  bool encode(char c);

  const GnssTelemetry& telemetry() const { return telemetry_; }

  uint32_t validSentences() const { return validSentences_; }
  uint32_t checksumErrors() const { return checksumErrors_; }

  // 現在見えている衛星を信号強度の強い順にoutへコピーし、その数を返す。
  // GGAを受信するたびにエポックが進み、直近2エポックに報告された衛星を「現在」とみなす。
  size_t currentSatellites(SatelliteInfo* out, size_t max) const;

  static const size_t kMaxSatellites = 64;

  static const char* fixQualityName(uint8_t quality);
  static const char* systemName(GnssSystem system);
  // RINEX形式の1文字略号 (G=GPS, R=GLONASS, E=Galileo, C=BeiDou, J=QZSS, I=NavIC)
  static char systemLetter(GnssSystem system);

 private:
  static const size_t kMaxSentence = 128;
  static const size_t kMaxFields = 32;

  bool processSentence();
  void parseGga(char** fields, size_t count);
  void parseRmc(char** fields, size_t count);
  void parseGsa(char** fields, size_t count);
  void parseGsv(char** fields, size_t count, GnssSystem system);
  void updateSatellite(GnssSystem system, uint8_t prn, int8_t elevation, int16_t azimuth, uint8_t snr);
  bool isCurrent(const SatelliteInfo& sat) const;
  bool parseTime(const char* field);

  char buffer_[kMaxSentence];
  size_t length_ = 0;
  bool inSentence_ = false;

  GnssTelemetry telemetry_;
  SatelliteInfo satellites_[kMaxSatellites];
  uint32_t epoch_ = 0;
  uint32_t validSentences_ = 0;
  uint32_t checksumErrors_ = 0;
};
