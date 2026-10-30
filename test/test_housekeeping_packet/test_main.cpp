#include <HousekeepingPacket.h>
#include <unity.h>

namespace {

// パケットの副ヘッダの機上時刻。バイトの並びがわかりやすい値にしておく
const uint64_t kPacketTimeMs = 0x0102030405060708ULL;

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
  size_t n = HousekeepingPacket::encode(hk, 7, kPacketTimeMs, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(66, n);

  // version=0, type=TM, secondary header=1, APID=101
  TEST_ASSERT_EQUAL_HEX16(0x0865, readU16(buf));
  TEST_ASSERT_EQUAL_HEX16(0xC007, readU16(buf + 2));
  // データ部60バイト (副ヘッダ8 + 中身52) → length=59
  TEST_ASSERT_EQUAL_UINT16(59, readU16(buf + 4));
  // 副ヘッダ: 機上時刻 (ミリ秒、ビッグエンディアン)
  const uint8_t time[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(time, buf + 6, 8);
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
  hk.fdirEnabled = false;
  hk.lastFault = HousekeepingPacket::Fault::GnssNoData;
  hk.faultCount = 513;
  hk.onboardTime = 1791166425;
  hk.observationState = HousekeepingPacket::ObservationState::Scheduled;
  hk.observationStart = 1791166725;
  hk.observationDuration = 180;
  hk.lastObservationResult = HousekeepingPacket::ObservationResult::Aborted;
  hk.lastObservationStart = 1791166125;
  hk.lastObservationLength = 75;
  hk.completedObservations = 260;
  hk.lampColor = HousekeepingPacket::LampColor::Magenta;
  hk.lampRemaining = 590;
  hk.payloadOnTime = 1234;

  uint8_t buf[HousekeepingPacket::kPacketSize];
  HousekeepingPacket::encode(hk, 0, kPacketTimeMs, buf, sizeof(buf));

  TEST_ASSERT_EQUAL_UINT8(2, buf[14]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[15]);
  TEST_ASSERT_EQUAL_UINT16(300, readU16(buf + 16));
  TEST_ASSERT_EQUAL_UINT16(2, readU16(buf + 18));
  TEST_ASSERT_EQUAL_UINT8(5, buf[20]);
  TEST_ASSERT_EQUAL_INT8(-67, static_cast<int8_t>(buf[21]));
  TEST_ASSERT_EQUAL_UINT32(180000, readU32(buf + 22));
  TEST_ASSERT_EQUAL_UINT32(150000, readU32(buf + 26));
  TEST_ASSERT_EQUAL_UINT32(123456, readU32(buf + 30));
  TEST_ASSERT_EQUAL_UINT16(42, readU16(buf + 34));
  TEST_ASSERT_EQUAL_UINT8(3, buf[36]);
  TEST_ASSERT_EQUAL_UINT8(0, buf[37]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[38]);
  TEST_ASSERT_EQUAL_UINT16(513, readU16(buf + 39));
  TEST_ASSERT_EQUAL_UINT32(1791166425, readU32(buf + 41));
  TEST_ASSERT_EQUAL_UINT8(1, buf[45]);
  TEST_ASSERT_EQUAL_UINT32(1791166725, readU32(buf + 46));
  TEST_ASSERT_EQUAL_UINT16(180, readU16(buf + 50));
  TEST_ASSERT_EQUAL_UINT8(2, buf[52]);
  TEST_ASSERT_EQUAL_UINT32(1791166125, readU32(buf + 53));
  TEST_ASSERT_EQUAL_UINT16(75, readU16(buf + 57));
  TEST_ASSERT_EQUAL_UINT16(260, readU16(buf + 59));
  TEST_ASSERT_EQUAL_UINT8(6, buf[61]);
  TEST_ASSERT_EQUAL_UINT16(590, readU16(buf + 62));
  TEST_ASSERT_EQUAL_UINT16(1234, readU16(buf + 64));
}

void test_defaults() {
  // 5-2 / 5-4 で切り替えを実装するまでは NOMINAL・ペイロードON
  HousekeepingPacket::Housekeeping hk;
  uint8_t buf[HousekeepingPacket::kPacketSize];
  HousekeepingPacket::encode(hk, 0, kPacketTimeMs, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT8(1, buf[14]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[15]);
  TEST_ASSERT_EQUAL_UINT8(1, buf[37]);  // FDIRは起動時に有効
  TEST_ASSERT_EQUAL_UINT8(0, buf[38]);  // 異常なし
  TEST_ASSERT_EQUAL_UINT8(0, buf[52]);  // まだ観測していない
}

void test_buffer_too_small() {
  HousekeepingPacket::Housekeeping hk;
  uint8_t buf[HousekeepingPacket::kPacketSize - 1];
  TEST_ASSERT_EQUAL(0, HousekeepingPacket::encode(hk, 0, kPacketTimeMs, buf, sizeof(buf)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_primary_header);
  RUN_TEST(test_payload);
  RUN_TEST(test_defaults);
  RUN_TEST(test_buffer_too_small);
  return UNITY_END();
}
