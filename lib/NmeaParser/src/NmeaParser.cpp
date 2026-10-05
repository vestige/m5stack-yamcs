#include "NmeaParser.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace {

// Talker ID（アドレスの先頭2文字）から衛星システムを判定する
GnssSystem systemFromTalker(const char* address) {
  char a = address[0];
  char b = address[1];
  if (a == 'G' && b == 'P') return GnssSystem::GPS;
  if (a == 'G' && b == 'L') return GnssSystem::GLONASS;
  if (a == 'G' && b == 'A') return GnssSystem::Galileo;
  if ((a == 'G' && b == 'B') || (a == 'B' && b == 'D')) return GnssSystem::BeiDou;
  if ((a == 'G' && b == 'Q') || (a == 'Q' && b == 'Z')) return GnssSystem::QZSS;
  if (a == 'G' && b == 'I') return GnssSystem::NavIC;
  return GnssSystem::Unknown;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool isEmpty(const char* field) { return field == nullptr || field[0] == '\0'; }

int twoDigits(const char* p) { return (p[0] - '0') * 10 + (p[1] - '0'); }

bool allDigits(const char* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
  }
  return true;
}

// "ddmm.mmmm" / "dddmm.mmmm" と半球記号を度に変換する
bool parseCoordinate(const char* value, const char* hemisphere, double* out) {
  if (isEmpty(value) || isEmpty(hemisphere)) return false;

  char* end = nullptr;
  double raw = strtod(value, &end);
  if (end == value) return false;

  int degrees = static_cast<int>(raw / 100);
  double minutes = raw - degrees * 100.0;
  double result = degrees + minutes / 60.0;

  if (hemisphere[0] == 'S' || hemisphere[0] == 'W') result = -result;
  *out = result;
  return true;
}

}  // namespace

bool NmeaParser::encode(char c) {
  if (c == '$') {
    inSentence_ = true;
    length_ = 0;
    return false;
  }
  if (!inSentence_) return false;

  if (c == '\r' || c == '\n') {
    inSentence_ = false;
    buffer_[length_] = '\0';
    return processSentence();
  }

  if (length_ >= kMaxSentence - 1) {
    // 長すぎるセンテンスは破棄
    inSentence_ = false;
    return false;
  }
  buffer_[length_++] = c;
  return false;
}

bool NmeaParser::processSentence() {
  // チェックサム検証: '$' と '*' の間のXOR
  char* star = strchr(buffer_, '*');
  if (star == nullptr || strlen(star) < 3) {
    checksumErrors_++;
    return false;
  }
  int hi = hexValue(star[1]);
  int lo = hexValue(star[2]);
  if (hi < 0 || lo < 0) {
    checksumErrors_++;
    return false;
  }
  uint8_t expected = static_cast<uint8_t>((hi << 4) | lo);
  uint8_t actual = 0;
  for (char* p = buffer_; p < star; p++) actual ^= static_cast<uint8_t>(*p);
  if (actual != expected) {
    checksumErrors_++;
    return false;
  }
  *star = '\0';

  // カンマで分割（空フィールドも保持する）
  char* fields[kMaxFields];
  size_t count = 0;
  char* p = buffer_;
  fields[count++] = p;
  while (*p != '\0' && count < kMaxFields) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
    p++;
  }

  validSentences_++;

  // アドレスは "GNGGA" のように Talker ID(2文字) + Sentence type(3文字)
  const char* address = fields[0];
  if (strlen(address) < 5) return true;
  const char* type = address + strlen(address) - 3;

  if (strcmp(type, "GGA") == 0) {
    parseGga(fields, count);
  } else if (strcmp(type, "RMC") == 0) {
    parseRmc(fields, count);
  } else if (strcmp(type, "GSA") == 0) {
    parseGsa(fields, count);
  } else if (strcmp(type, "GSV") == 0) {
    parseGsv(fields, count, systemFromTalker(address));
  }
  return true;
}

bool NmeaParser::parseTime(const char* field) {
  // hhmmss.sss
  if (isEmpty(field) || strlen(field) < 6 || !allDigits(field, 6)) return false;

  telemetry_.hour = twoDigits(field);
  telemetry_.minute = twoDigits(field + 2);
  telemetry_.second = twoDigits(field + 4);
  telemetry_.millisecond = 0;
  if (field[6] == '.') {
    double fraction = strtod(field + 6, nullptr);
    telemetry_.millisecond = static_cast<uint16_t>(fraction * 1000.0 + 0.5);
  }
  telemetry_.timeValid = true;
  return true;
}

// $xxGGA,time,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,sep,M,diffAge,diffStation
void NmeaParser::parseGga(char** fields, size_t count) {
  if (count < 10) return;

  // GGAは毎秒1回出力されるので、これを衛星表のエポックの区切りにする
  epoch_++;

  parseTime(fields[1]);

  telemetry_.fixQuality = isEmpty(fields[6]) ? 0 : static_cast<uint8_t>(atoi(fields[6]));
  telemetry_.satellites = isEmpty(fields[7]) ? 0 : static_cast<uint8_t>(atoi(fields[7]));

  double lat = 0.0;
  double lon = 0.0;
  bool hasPosition = telemetry_.fixQuality != 0 && parseCoordinate(fields[2], fields[3], &lat) &&
                     parseCoordinate(fields[4], fields[5], &lon);
  telemetry_.positionValid = hasPosition;
  if (hasPosition) {
    telemetry_.latitude = lat;
    telemetry_.longitude = lon;
  }

  telemetry_.altitudeValid = telemetry_.fixQuality != 0 && !isEmpty(fields[9]);
  if (telemetry_.altitudeValid) telemetry_.altitude = static_cast<float>(atof(fields[9]));
}

// $xxRMC,time,status,lat,N/S,lon,E/W,speed,course,date,magVar,magVarEW,mode
void NmeaParser::parseRmc(char** fields, size_t count) {
  if (count < 10) return;

  parseTime(fields[1]);
  telemetry_.rmcActive = !isEmpty(fields[2]) && fields[2][0] == 'A';

  // ddmmyy
  const char* date = fields[9];
  if (!isEmpty(date) && strlen(date) >= 6 && allDigits(date, 6)) {
    telemetry_.day = twoDigits(date);
    telemetry_.month = twoDigits(date + 2);
    telemetry_.year = 2000 + twoDigits(date + 4);
    telemetry_.dateValid = true;
  }
}

// $xxGSA,mode,fixType,sv1..sv12,PDOP,HDOP,VDOP[,systemId]
void NmeaParser::parseGsa(char** fields, size_t count) {
  if (count < 3 || isEmpty(fields[2])) return;
  int type = atoi(fields[2]);
  if (type >= 1 && type <= 3) telemetry_.fixType = static_cast<uint8_t>(type);
}

// $xxGSV,numMsg,msgNo,numSV,{prn,elev,az,cn0}x1..4[,signalId]
void NmeaParser::parseGsv(char** fields, size_t count, GnssSystem system) {
  if (count < 8) return;

  // 衛星1機あたり4フィールド。NMEA 4.10以降は末尾にsignalIdが付くが、整数除算で無視される
  size_t entries = (count - 4) / 4;
  for (size_t i = 0; i < entries; i++) {
    char** sat = fields + 4 + i * 4;
    if (isEmpty(sat[0])) continue;
    int prn = atoi(sat[0]);
    if (prn <= 0 || prn > 255) continue;

    int8_t elevation = isEmpty(sat[1]) ? -1 : static_cast<int8_t>(atoi(sat[1]));
    int16_t azimuth = isEmpty(sat[2]) ? -1 : static_cast<int16_t>(atoi(sat[2]));
    uint8_t snr = isEmpty(sat[3]) ? 0 : static_cast<uint8_t>(atoi(sat[3]));
    updateSatellite(system, static_cast<uint8_t>(prn), elevation, azimuth, snr);
  }
}

void NmeaParser::updateSatellite(GnssSystem system, uint8_t prn, int8_t elevation, int16_t azimuth,
                                 uint8_t snr) {
  SatelliteInfo* slot = nullptr;
  SatelliteInfo* oldest = &satellites_[0];
  for (SatelliteInfo& sat : satellites_) {
    if (sat.prn == prn && sat.system == system) {
      slot = &sat;
      break;
    }
    if (sat.prn == 0 || sat.epoch < oldest->epoch) oldest = &sat;
    if (sat.prn == 0) break;
  }

  if (slot == nullptr) {
    // 新しい衛星: 空きスロットか、最も古いスロットを使う
    slot = oldest;
    *slot = SatelliteInfo();
    slot->system = system;
    slot->prn = prn;
  } else if (slot->epoch == epoch_) {
    // 同じエポックで別の信号(L1/L5など)として報告された場合は強い方を残す
    snr = std::max(slot->snr, snr);
  }

  if (elevation >= 0) slot->elevation = elevation;
  if (azimuth >= 0) slot->azimuth = azimuth;
  slot->snr = snr;
  slot->epoch = epoch_;
}

bool NmeaParser::isCurrent(const SatelliteInfo& sat) const {
  return sat.prn != 0 && epoch_ - sat.epoch <= 1;
}

size_t NmeaParser::currentSatellites(SatelliteInfo* out, size_t max) const {
  size_t n = 0;
  for (const SatelliteInfo& sat : satellites_) {
    if (n >= max) break;
    if (isCurrent(sat)) out[n++] = sat;
  }
  std::sort(out, out + n, [](const SatelliteInfo& a, const SatelliteInfo& b) {
    if (a.snr != b.snr) return a.snr > b.snr;
    if (a.system != b.system) return a.system < b.system;
    return a.prn < b.prn;
  });
  return n;
}

const char* NmeaParser::systemName(GnssSystem system) {
  switch (system) {
    case GnssSystem::GPS: return "GPS";
    case GnssSystem::GLONASS: return "GLONASS";
    case GnssSystem::Galileo: return "Galileo";
    case GnssSystem::BeiDou: return "BeiDou";
    case GnssSystem::QZSS: return "QZSS";
    case GnssSystem::NavIC: return "NavIC";
    default: return "Unknown";
  }
}

char NmeaParser::systemLetter(GnssSystem system) {
  switch (system) {
    case GnssSystem::GPS: return 'G';
    case GnssSystem::GLONASS: return 'R';
    case GnssSystem::Galileo: return 'E';
    case GnssSystem::BeiDou: return 'C';
    case GnssSystem::QZSS: return 'J';
    case GnssSystem::NavIC: return 'I';
    default: return '?';
  }
}

const char* NmeaParser::fixQualityName(uint8_t quality) {
  switch (quality) {
    case 0: return "NoFix";
    case 1: return "GPS";
    case 2: return "DGPS";
    case 3: return "PPS";
    case 4: return "RTK";
    case 5: return "FloatRTK";
    case 6: return "Estimated";
    case 7: return "Manual";
    case 8: return "Simulation";
    default: return "Unknown";
  }
}
