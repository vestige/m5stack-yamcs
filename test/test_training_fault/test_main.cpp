#include <Telecommand.h>
#include <TrainingFault.h>
#include <unity.h>

using TrainingFault::Kind;
using TrainingFault::Request;

void setUp() {}
void tearDown() {}

namespace {

size_t encode(Kind kind, uint16_t durationSec, uint8_t* buf) {
  Request request;
  request.kind = kind;
  request.durationSec = durationSec;
  return TrainingFault::encode(request, 3, buf, TrainingFault::kPacketSize);
}

}  // namespace

void test_packet_round_trip() {
  uint8_t buf[TrainingFault::kPacketSize];
  TEST_ASSERT_EQUAL(TrainingFault::kPacketSize, encode(Kind::GnssStop, 90, buf));
  // version=0, type=TC, APID=111
  TEST_ASSERT_EQUAL_HEX8(0x10, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x6F, buf[1]);
  TEST_ASSERT_TRUE(TrainingFault::isTrainingPacket(buf, sizeof(buf)));
  Request request;
  TEST_ASSERT_TRUE(TrainingFault::parse(buf, sizeof(buf), &request));
  TEST_ASSERT_TRUE(request.kind == Kind::GnssStop);
  TEST_ASSERT_EQUAL_UINT16(90, request.durationSec);
}

void test_ordinary_telecommand_is_not_training() {
  // ふつうのコマンド (APID 110) は訓練用ではない
  uint8_t tc[] = {0x10, 0x6E, 0xC0, 0x00, 0x00, 0x00, 0x01};
  TEST_ASSERT_FALSE(TrainingFault::isTrainingPacket(tc, sizeof(tc)));
  // 訓練用のパケットは、ふつうのコマンドとしては受け付けない
  uint8_t buf[TrainingFault::kPacketSize];
  encode(Kind::Reboot, 0, buf);
  Telecommand::Packet packet;
  bool hasHeader;
  TEST_ASSERT_TRUE(Telecommand::parse(buf, sizeof(buf), &packet, &hasHeader) ==
                   Telecommand::ErrorCode::MalformedPacket);
}

void test_invalid_packets_are_rejected() {
  uint8_t buf[TrainingFault::kPacketSize];
  Request request;
  encode(Kind::GnssStop, 10, buf);
  TEST_ASSERT_FALSE(TrainingFault::parse(buf, sizeof(buf) - 1, &request));  // 短い
  buf[6] = 9;                                                                // 知らない異常
  TEST_ASSERT_FALSE(TrainingFault::parse(buf, sizeof(buf), &request));
  encode(Kind::GnssStop, 10, buf);
  buf[5] = 5;  // 長さのフィールドが合わない
  TEST_ASSERT_FALSE(TrainingFault::parse(buf, sizeof(buf), &request));
}

void test_gnss_stop_lasts_for_duration() {
  TrainingFaults faults;
  TEST_ASSERT_FALSE(faults.gnssStopped(1000));
  faults.apply({Kind::GnssStop, 60}, 1000);
  TEST_ASSERT_TRUE(faults.gnssStopped(1000));
  TEST_ASSERT_TRUE(faults.gnssStopped(60999));
  TEST_ASSERT_FALSE(faults.gnssStopped(61000));
  TEST_ASSERT_FALSE(faults.commandsIgnored(2000));  // ほかの異常は起きない
}

void test_command_loss_and_clear() {
  TrainingFaults faults;
  faults.apply({Kind::CommandLoss, 30}, 0xFFFFF000);  // millis が一周する前後でも数えられる
  TEST_ASSERT_TRUE(faults.commandsIgnored(0xFFFFF000 + 29000));
  TEST_ASSERT_FALSE(faults.commandsIgnored(0xFFFFF000 + 30000));

  faults.apply({Kind::CommandLoss, 300}, 1000);
  faults.apply({Kind::GnssStop, 300}, 1000);
  faults.apply({Kind::Clear, 0}, 2000);
  TEST_ASSERT_FALSE(faults.commandsIgnored(3000));
  TEST_ASSERT_FALSE(faults.gnssStopped(3000));
}

void test_reboot_request_is_taken_once() {
  TrainingFaults faults;
  TEST_ASSERT_FALSE(faults.takeRebootRequest());
  faults.apply({Kind::Reboot, 0}, 1000);
  TEST_ASSERT_TRUE(faults.takeRebootRequest());
  TEST_ASSERT_FALSE(faults.takeRebootRequest());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_packet_round_trip);
  RUN_TEST(test_ordinary_telecommand_is_not_training);
  RUN_TEST(test_invalid_packets_are_rejected);
  RUN_TEST(test_gnss_stop_lasts_for_duration);
  RUN_TEST(test_command_loss_and_clear);
  RUN_TEST(test_reboot_request_is_taken_once);
  return UNITY_END();
}
