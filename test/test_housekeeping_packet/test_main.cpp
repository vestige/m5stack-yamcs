#include <HousekeepingPacket.h>
#include <unity.h>

namespace {

uint32_t readU32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

}  // namespace

void setUp() {}
void tearDown() {}

void test_primary_header() {
  HousekeepingPacket::Housekeeping hk;
  uint8_t buf[HousekeepingPacket::kPacketSize];
  size_t n = HousekeepingPacket::encode(hk, 7, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(29, n);

  // version=0, type=TM, secondary header=0, APID=101
  TEST_ASSERT_EQUAL_HEX16(0x0065, readU16(buf));
  TEST_ASSERT_EQUAL_HEX16(0xC007, readU16(buf + 2));
  // データ部23バイト → length=22
  TEST_ASSERT_EQUAL_UINT16(22, readU16(buf + 4));
}

void test_payload() {
  HousekeepingPacket::Housekeeping hk;
  hk.mode = HousekeepingPacket::OperationMode::Mission;
  hk.payloadPower = true;
  hk.acceptedCommands = 300;
  hk.rejectedCommands = 2;
  hk.lastCommandId = 5;
  hk.wifiRssi = -67;
  hk.freeHeap = 180000;
  hk.minFreeHeap = 150000;
  hk.uptimeMs = 123456;
  hk.bootCount = 42;
  hk.resetReason = 3;  // ESP_RST_SW

  uint8_t buf[HousekeepingPacket::kPacketSize];
  HousekeepingPacket::encode(hk, 0, buf, sizeof(buf));

  TEST_ASSERT_EQUAL_UINT8(2, buf[6]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[7]);
  TEST_ASSERT_EQUAL_UINT16(300, readU16(buf + 8));
  TEST_ASSERT_EQUAL_UINT16(2, readU16(buf + 10));
  TEST_ASSERT_EQUAL_UINT8(5, buf[12]);
  TEST_ASSERT_EQUAL_INT8(-67, static_cast<int8_t>(buf[13]));
  TEST_ASSERT_EQUAL_UINT32(180000, readU32(buf + 14));
  TEST_ASSERT_EQUAL_UINT32(150000, readU32(buf + 18));
  TEST_ASSERT_EQUAL_UINT32(123456, readU32(buf + 22));
  TEST_ASSERT_EQUAL_UINT16(42, readU16(buf + 26));
  TEST_ASSERT_EQUAL_UINT8(3, buf[28]);
}

void test_defaults() {
  // 5-2 / 5-4 で切り替えを実装するまでは NOMINAL・ペイロードON
  HousekeepingPacket::Housekeeping hk;
  uint8_t buf[HousekeepingPacket::kPacketSize];
  HousekeepingPacket::encode(hk, 0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT8(1, buf[6]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[7]);
}

void test_buffer_too_small() {
  HousekeepingPacket::Housekeeping hk;
  uint8_t buf[HousekeepingPacket::kPacketSize - 1];
  TEST_ASSERT_EQUAL(0, HousekeepingPacket::encode(hk, 0, buf, sizeof(buf)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_primary_header);
  RUN_TEST(test_payload);
  RUN_TEST(test_defaults);
  RUN_TEST(test_buffer_too_small);
  return UNITY_END();
}
