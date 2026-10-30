#include <EventPacket.h>
#include <HousekeepingPacket.h>
#include <RecordFormat.h>
#include <TelemetryPacket.h>
#include <unity.h>

using RecordFormat::Source;

void setUp() {}
void tearDown() {}

namespace {

const uint64_t kTimeMs = 1793341480549ULL;  // 2026-10-30T06:24:40.549Z

}  // namespace

void test_source_by_apid() {
  uint8_t buf[HousekeepingPacket::kPacketSize];
  HousekeepingPacket::Housekeeping hk;
  HousekeepingPacket::encode(hk, 0, kTimeMs, buf, sizeof(buf));
  Source source;
  TEST_ASSERT_TRUE(RecordFormat::sourceOf(buf, sizeof(buf), &source));
  TEST_ASSERT_TRUE(source == Source::Bus);

  uint8_t gnss[TelemetryPacket::kPacketSize];
  TelemetryPacket::encode(GnssTelemetry(), 0, 0, 0, 0, kTimeMs, gnss, sizeof(gnss));
  TEST_ASSERT_TRUE(RecordFormat::sourceOf(gnss, sizeof(gnss), &source));
  TEST_ASSERT_TRUE(source == Source::Gnss);

  // TC (APID 110) や知らない APID は記録しない
  uint8_t tc[] = {0x10, 0x6E, 0xC0, 0x00, 0x00, 0x00, 0x01};
  TEST_ASSERT_FALSE(RecordFormat::sourceOf(tc, sizeof(tc), &source));
  uint8_t unknown[] = {0x08, 0x10, 0xC0, 0x00, 0x00, 0x07, 0, 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(RecordFormat::sourceOf(unknown, sizeof(unknown), &source));
}

void test_packet_time() {
  uint8_t buf[EventPacket::kPacketSize];
  EventPacket::encode(EventPacket::Event(), 0, kTimeMs, buf, sizeof(buf));
  TEST_ASSERT_TRUE(RecordFormat::packetTimeMs(buf, sizeof(buf)) == kTimeMs);
  EventPacket::encode(EventPacket::Event(), 0, 0, buf, sizeof(buf));
  TEST_ASSERT_TRUE(RecordFormat::packetTimeMs(buf, sizeof(buf)) == 0);
  TEST_ASSERT_TRUE(RecordFormat::packetTimeMs(buf, 10) == 0);  // 副ヘッダの分が無い
}

void test_file_path() {
  char path[RecordFormat::kMaxPathLength];
  RecordFormat::filePath(Source::Gnss, static_cast<uint32_t>(kTimeMs / 1000), path, sizeof(path));
  TEST_ASSERT_EQUAL_STRING("/rec/gnss/20261030/06.bin", path);
  RecordFormat::filePath(Source::Bus, 1793404799, path, sizeof(path));  // 2026-10-30T23:59:59Z
  TEST_ASSERT_EQUAL_STRING("/rec/bus/20261030/23.bin", path);
  RecordFormat::sourceDir(Source::Bus, path, sizeof(path));
  TEST_ASSERT_EQUAL_STRING("/rec/bus", path);
}

void test_length() {
  uint8_t out[2];
  RecordFormat::writeLength(219, out);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xDB, out[1]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_source_by_apid);
  RUN_TEST(test_packet_time);
  RUN_TEST(test_file_path);
  RUN_TEST(test_length);
  return UNITY_END();
}
