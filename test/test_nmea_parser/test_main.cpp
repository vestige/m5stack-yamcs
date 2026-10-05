#include <NmeaParser.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

namespace {

// チェックサムを計算して "$<body>*HH\r\n" を組み立てる
std::string sentence(const char* body) {
  uint8_t sum = 0;
  for (const char* p = body; *p != '\0'; p++) sum ^= static_cast<uint8_t>(*p);
  char tail[8];
  snprintf(tail, sizeof(tail), "*%02X\r\n", sum);
  return std::string("$") + body + tail;
}

int feed(NmeaParser& parser, const std::string& data) {
  int processed = 0;
  for (char c : data) {
    if (parser.encode(c)) processed++;
  }
  return processed;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_gga_with_known_checksum() {
  NmeaParser parser;
  int n = feed(parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
  TEST_ASSERT_EQUAL(1, n);

  const GnssTelemetry& t = parser.telemetry();
  TEST_ASSERT_TRUE(t.timeValid);
  TEST_ASSERT_EQUAL(12, t.hour);
  TEST_ASSERT_EQUAL(35, t.minute);
  TEST_ASSERT_EQUAL(19, t.second);
  TEST_ASSERT_TRUE(t.positionValid);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 48.1173, t.latitude);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 11.516666667, t.longitude);
  TEST_ASSERT_TRUE(t.altitudeValid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 545.4f, t.altitude);
  TEST_ASSERT_EQUAL(8, t.satellites);
  TEST_ASSERT_EQUAL(1, t.fixQuality);
}

void test_rmc_with_known_checksum() {
  NmeaParser parser;
  int n = feed(parser, "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n");
  TEST_ASSERT_EQUAL(1, n);

  const GnssTelemetry& t = parser.telemetry();
  TEST_ASSERT_TRUE(t.rmcActive);
  TEST_ASSERT_TRUE(t.dateValid);
  TEST_ASSERT_EQUAL(2094, t.year);  // 2桁年は2000年代として扱う
  TEST_ASSERT_EQUAL(3, t.month);
  TEST_ASSERT_EQUAL(23, t.day);
}

void test_gn_talker_and_south_west() {
  NmeaParser parser;
  feed(parser, sentence("GNGGA,021345.250,3335.4213,S,13024.1030,W,1,12,0.8,12.3,M,27.0,M,,"));

  const GnssTelemetry& t = parser.telemetry();
  TEST_ASSERT_EQUAL(250, t.millisecond);
  TEST_ASSERT_TRUE(t.positionValid);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, -(33 + 35.4213 / 60.0), t.latitude);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, -(130 + 24.1030 / 60.0), t.longitude);
  TEST_ASSERT_EQUAL(12, t.satellites);
}

void test_gga_without_fix() {
  NmeaParser parser;
  feed(parser, sentence("GNGGA,021345.000,,,,,0,00,99.99,,,,,,"));

  const GnssTelemetry& t = parser.telemetry();
  TEST_ASSERT_TRUE(t.timeValid);
  TEST_ASSERT_FALSE(t.positionValid);
  TEST_ASSERT_FALSE(t.altitudeValid);
  TEST_ASSERT_EQUAL(0, t.satellites);
  TEST_ASSERT_EQUAL(0, t.fixQuality);
}

void test_gga_without_time() {
  NmeaParser parser;
  feed(parser, sentence("GNGGA,,,,,,0,00,,,,,,,"));
  TEST_ASSERT_FALSE(parser.telemetry().timeValid);
}

void test_gsa_fix_type() {
  NmeaParser parser;
  feed(parser, sentence("GNGSA,A,3,05,13,15,18,,,,,,,,,1.5,0.8,1.2,1"));
  TEST_ASSERT_EQUAL(3, parser.telemetry().fixType);

  feed(parser, sentence("GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,1"));
  TEST_ASSERT_EQUAL(1, parser.telemetry().fixType);
}

void test_rmc_void() {
  NmeaParser parser;
  feed(parser, sentence("GNRMC,021345.000,V,,,,,,,,,,N"));
  TEST_ASSERT_FALSE(parser.telemetry().rmcActive);
  TEST_ASSERT_FALSE(parser.telemetry().dateValid);
}

void test_bad_checksum_is_rejected() {
  NmeaParser parser;
  int n = feed(parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48\r\n");
  TEST_ASSERT_EQUAL(0, n);
  TEST_ASSERT_EQUAL(1, parser.checksumErrors());
  TEST_ASSERT_FALSE(parser.telemetry().positionValid);
}

void test_missing_checksum_is_rejected() {
  NmeaParser parser;
  int n = feed(parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,\r\n");
  TEST_ASSERT_EQUAL(0, n);
  TEST_ASSERT_EQUAL(1, parser.checksumErrors());
}

void test_other_sentences_are_counted_but_ignored() {
  NmeaParser parser;
  int n = feed(parser, sentence("GPGSV,3,1,11,05,45,123,40,13,30,200,35,15,60,045,42,18,10,300,"));
  TEST_ASSERT_EQUAL(1, n);
  TEST_ASSERT_EQUAL(1, parser.validSentences());
  TEST_ASSERT_FALSE(parser.telemetry().timeValid);
}

void test_garbage_and_stream_recovery() {
  NmeaParser parser;
  std::string stream = "noise$GPGGA,trunc";  // 途中で切れたセンテンス
  stream += sentence("GNGGA,000001.000,3500.0000,N,13500.0000,E,1,05,1.0,10.0,M,0.0,M,,");
  stream += sentence("GNRMC,000001.000,A,3500.0000,N,13500.0000,E,0.0,0.0,051026,,,A");
  int n = feed(parser, stream);
  TEST_ASSERT_EQUAL(2, n);

  const GnssTelemetry& t = parser.telemetry();
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 35.0, t.latitude);
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 135.0, t.longitude);
  TEST_ASSERT_EQUAL(2026, t.year);
  TEST_ASSERT_EQUAL(10, t.month);
  TEST_ASSERT_EQUAL(5, t.day);
}

void test_overlong_sentence_is_discarded() {
  NmeaParser parser;
  std::string body(200, 'A');
  int n = feed(parser, sentence(body.c_str()));
  TEST_ASSERT_EQUAL(0, n);

  n = feed(parser, sentence("GNGSA,A,2,,,,,,,,,,,,,1.0,1.0,1.0,1"));
  TEST_ASSERT_EQUAL(1, n);
  TEST_ASSERT_EQUAL(2, parser.telemetry().fixType);
}

void test_gsv_satellites() {
  NmeaParser parser;
  feed(parser, sentence("GPGSV,2,1,05,05,45,123,40,13,30,200,35,15,60,045,42,18,10,300,"));
  feed(parser, sentence("GPGSV,2,2,05,29,05,010,"));

  SatelliteInfo sats[NmeaParser::kMaxSatellites];
  size_t n = parser.currentSatellites(sats, NmeaParser::kMaxSatellites);
  TEST_ASSERT_EQUAL(5, n);

  // 信号強度の強い順。捕捉していない衛星(SNR空欄)は0として末尾に並ぶ
  TEST_ASSERT_EQUAL(15, sats[0].prn);
  TEST_ASSERT_EQUAL(42, sats[0].snr);
  TEST_ASSERT_EQUAL(60, sats[0].elevation);
  TEST_ASSERT_EQUAL(45, sats[0].azimuth);
  TEST_ASSERT_EQUAL(5, sats[1].prn);
  TEST_ASSERT_EQUAL(13, sats[2].prn);
  TEST_ASSERT_EQUAL(0, sats[3].snr);
  TEST_ASSERT_EQUAL(0, sats[4].snr);
  for (size_t i = 0; i < n; i++) TEST_ASSERT_TRUE(sats[i].system == GnssSystem::GPS);
}

void test_gsv_talker_to_system() {
  NmeaParser parser;
  feed(parser, sentence("GLGSV,1,1,01,70,40,100,30,1"));
  feed(parser, sentence("GAGSV,1,1,01,11,40,100,31,7"));
  feed(parser, sentence("GBGSV,1,1,01,23,40,100,32,1"));
  feed(parser, sentence("BDGSV,1,1,01,24,40,100,33,1"));
  feed(parser, sentence("GQGSV,1,1,01,02,80,180,34,1"));

  SatelliteInfo sats[NmeaParser::kMaxSatellites];
  size_t n = parser.currentSatellites(sats, NmeaParser::kMaxSatellites);
  TEST_ASSERT_EQUAL(5, n);
  TEST_ASSERT_TRUE(sats[0].system == GnssSystem::QZSS);  // 34
  TEST_ASSERT_TRUE(sats[1].system == GnssSystem::BeiDou);  // 33
  TEST_ASSERT_TRUE(sats[2].system == GnssSystem::BeiDou);  // 32
  TEST_ASSERT_TRUE(sats[3].system == GnssSystem::Galileo);  // 31
  TEST_ASSERT_TRUE(sats[4].system == GnssSystem::GLONASS);  // 30
  TEST_ASSERT_EQUAL('J', NmeaParser::systemLetter(sats[0].system));
  TEST_ASSERT_EQUAL_STRING("GLONASS", NmeaParser::systemName(sats[4].system));
}

void test_gsv_same_prn_on_different_systems() {
  NmeaParser parser;
  feed(parser, sentence("GPGSV,1,1,01,05,45,123,40"));
  feed(parser, sentence("GAGSV,1,1,01,05,30,200,35"));

  SatelliteInfo sats[NmeaParser::kMaxSatellites];
  TEST_ASSERT_EQUAL(2, parser.currentSatellites(sats, NmeaParser::kMaxSatellites));
}

void test_gsv_keeps_strongest_signal_in_same_epoch() {
  NmeaParser parser;
  // 同じ衛星がL1(signalId=1)とL5(signalId=8)で報告される
  feed(parser, sentence("GPGSV,1,1,01,05,45,123,38,1"));
  feed(parser, sentence("GPGSV,1,1,01,05,45,123,44,8"));
  feed(parser, sentence("GPGSV,1,1,01,05,45,123,30,1"));

  SatelliteInfo sats[NmeaParser::kMaxSatellites];
  TEST_ASSERT_EQUAL(1, parser.currentSatellites(sats, NmeaParser::kMaxSatellites));
  TEST_ASSERT_EQUAL(44, sats[0].snr);

  // エポックが変われば新しい値で置き換える
  feed(parser, sentence("GNGGA,000001.000,,,,,0,00,,,,,,,"));
  feed(parser, sentence("GPGSV,1,1,01,05,45,123,30,1"));
  TEST_ASSERT_EQUAL(1, parser.currentSatellites(sats, NmeaParser::kMaxSatellites));
  TEST_ASSERT_EQUAL(30, sats[0].snr);
}

void test_gsv_satellite_expires() {
  NmeaParser parser;
  feed(parser, sentence("GPGSV,1,1,01,05,45,123,40"));
  SatelliteInfo sats[NmeaParser::kMaxSatellites];

  feed(parser, sentence("GNGGA,000001.000,,,,,0,00,,,,,,,"));
  TEST_ASSERT_EQUAL(1, parser.currentSatellites(sats, NmeaParser::kMaxSatellites));

  feed(parser, sentence("GNGGA,000002.000,,,,,0,00,,,,,,,"));
  TEST_ASSERT_EQUAL(0, parser.currentSatellites(sats, NmeaParser::kMaxSatellites));
}

void test_gsv_table_full_reuses_oldest_slot() {
  NmeaParser parser;
  char body[64];
  for (int prn = 1; prn <= static_cast<int>(NmeaParser::kMaxSatellites); prn++) {
    snprintf(body, sizeof(body), "GPGSV,1,1,01,%02d,10,100,20", prn);
    feed(parser, sentence(body));
  }
  feed(parser, sentence("GNGGA,000001.000,,,,,0,00,,,,,,,"));
  feed(parser, sentence("GLGSV,1,1,01,70,40,100,30"));

  SatelliteInfo sats[NmeaParser::kMaxSatellites];
  size_t n = parser.currentSatellites(sats, NmeaParser::kMaxSatellites);
  TEST_ASSERT_EQUAL(NmeaParser::kMaxSatellites, n);
  TEST_ASSERT_EQUAL(70, sats[0].prn);
}

void test_fix_quality_name() {
  TEST_ASSERT_EQUAL_STRING("NoFix", NmeaParser::fixQualityName(0));
  TEST_ASSERT_EQUAL_STRING("GPS", NmeaParser::fixQualityName(1));
  TEST_ASSERT_EQUAL_STRING("Unknown", NmeaParser::fixQualityName(42));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_gga_with_known_checksum);
  RUN_TEST(test_rmc_with_known_checksum);
  RUN_TEST(test_gn_talker_and_south_west);
  RUN_TEST(test_gga_without_fix);
  RUN_TEST(test_gga_without_time);
  RUN_TEST(test_gsa_fix_type);
  RUN_TEST(test_rmc_void);
  RUN_TEST(test_bad_checksum_is_rejected);
  RUN_TEST(test_missing_checksum_is_rejected);
  RUN_TEST(test_other_sentences_are_counted_but_ignored);
  RUN_TEST(test_garbage_and_stream_recovery);
  RUN_TEST(test_overlong_sentence_is_discarded);
  RUN_TEST(test_gsv_satellites);
  RUN_TEST(test_gsv_talker_to_system);
  RUN_TEST(test_gsv_same_prn_on_different_systems);
  RUN_TEST(test_gsv_keeps_strongest_signal_in_same_epoch);
  RUN_TEST(test_gsv_satellite_expires);
  RUN_TEST(test_gsv_table_full_reuses_oldest_slot);
  RUN_TEST(test_fix_quality_name);
  return UNITY_END();
}
