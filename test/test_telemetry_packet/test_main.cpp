#include <TelemetryPacket.h>
#include <unity.h>

namespace {

uint32_t readU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

GnssTelemetry sampleTelemetry() {
  GnssTelemetry t;
  t.timeValid = true;
  t.hour = 2;
  t.minute = 13;
  t.second = 45;
  t.millisecond = 250;
  t.dateValid = true;
  t.year = 2026;
  t.month = 10;
  t.day = 5;
  t.positionValid = true;
  t.latitude = 35.681236;
  t.longitude = 139.767125;
  t.altitudeValid = true;
  t.altitude = 12.5f;
  t.satellites = 12;
  t.fixQuality = 1;
  t.fixType = 3;
  t.rmcActive = true;
  return t;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_unix_time() {
  TEST_ASSERT_EQUAL_UINT32(0, TelemetryPacket::toUnixTime(1970, 1, 1, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(951868800, TelemetryPacket::toUnixTime(2000, 3, 1, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(1709251199, TelemetryPacket::toUnixTime(2024, 2, 29, 23, 59, 59));
  TEST_ASSERT_EQUAL_UINT32(1791166425, TelemetryPacket::toUnixTime(2026, 10, 5, 2, 13, 45));
}

void test_primary_header() {
  uint8_t buf[TelemetryPacket::kPacketSize];
  size_t n = TelemetryPacket::encode(sampleTelemetry(), 0, 0, 0, 0x1234, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(48, n);

  // version=0, type=TM, secondary header=0, APID=100
  TEST_ASSERT_EQUAL_HEX16(0x0064, readU16(buf));
  // unsegmented, sequence count 0x1234
  TEST_ASSERT_EQUAL_HEX16(0xD234, readU16(buf + 2));
  // データ部42バイト → length=41
  TEST_ASSERT_EQUAL_UINT16(41, readU16(buf + 4));
}

void test_sequence_count_wraps_at_14_bits() {
  uint8_t buf[TelemetryPacket::kPacketSize];
  TelemetryPacket::encode(sampleTelemetry(), 0, 0, 0, 0x4001, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_HEX16(0xC001, readU16(buf + 2));
}

void test_payload() {
  uint8_t buf[TelemetryPacket::kPacketSize];
  TelemetryPacket::encode(sampleTelemetry(), 1520, 3, 123456, 0, buf, sizeof(buf));

  TEST_ASSERT_EQUAL_UINT32(1791166425, readU32(buf + 6));
  TEST_ASSERT_EQUAL_UINT16(250, readU16(buf + 10));

  // float64 latitude 35.681236 (IEEE 754, big endian)
  const uint8_t lat[] = {0x40, 0x41, 0xd7, 0x32, 0xbd, 0xc2, 0x6d, 0xce};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(lat, buf + 12, 8);
  // float32 altitude 12.5
  TEST_ASSERT_EQUAL_HEX32(0x41480000, readU32(buf + 28));

  TEST_ASSERT_EQUAL_UINT8(12, buf[32]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[33]);
  TEST_ASSERT_EQUAL_UINT8(3, buf[34]);
  TEST_ASSERT_EQUAL_HEX8(0x1F, buf[35]);  // すべてのフラグが立つ

  TEST_ASSERT_EQUAL_UINT32(123456, readU32(buf + 36));
  TEST_ASSERT_EQUAL_UINT32(1520, readU32(buf + 40));
  TEST_ASSERT_EQUAL_UINT32(3, readU32(buf + 44));
}

void test_invalid_values_are_zero() {
  GnssTelemetry t;  // 何も受信していない状態
  t.timeValid = true;  // 時刻だけあって日付が無い
  t.hour = 1;

  uint8_t buf[TelemetryPacket::kPacketSize];
  TelemetryPacket::encode(t, 0, 0, 0, 0, buf, sizeof(buf));

  TEST_ASSERT_EQUAL_UINT32(0, readU32(buf + 6));
  for (size_t i = 12; i < 32; i++) TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[34]);  // fix type: no fix
  TEST_ASSERT_EQUAL_HEX8(TelemetryPacket::kTimeValid, buf[35]);
}

void test_buffer_too_small() {
  uint8_t buf[TelemetryPacket::kPacketSize - 1];
  TEST_ASSERT_EQUAL(0, TelemetryPacket::encode(sampleTelemetry(), 0, 0, 0, 0, buf, sizeof(buf)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unix_time);
  RUN_TEST(test_primary_header);
  RUN_TEST(test_sequence_count_wraps_at_14_bits);
  RUN_TEST(test_payload);
  RUN_TEST(test_invalid_values_are_zero);
  RUN_TEST(test_buffer_too_small);
  return UNITY_END();
}
