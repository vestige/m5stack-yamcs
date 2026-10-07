#include <AckPacket.h>
#include <Satellite.h>
#include <Telecommand.h>
#include <unity.h>

#include <vector>

using Telecommand::CommandId;
using Telecommand::ErrorCode;
using OperationMode = Satellite::OperationMode;

namespace {

// YAMCSが作るのと同じ形のTCパケットを組み立てる
std::vector<uint8_t> tcPacket(uint8_t commandId, std::vector<uint8_t> args, uint16_t seq = 0,
                              uint16_t apid = Telecommand::kApid) {
  std::vector<uint8_t> p;
  uint16_t word1 = 0x1000 | (apid & 0x07FF);  // version=0, type=TC
  uint16_t word2 = 0xC000 | (seq & 0x3FFF);
  size_t size = Telecommand::kPrimaryHeaderSize + 1 + args.size();
  uint16_t length = static_cast<uint16_t>(size - Telecommand::kPrimaryHeaderSize - 1);
  p.push_back(word1 >> 8);
  p.push_back(word1 & 0xFF);
  p.push_back(word2 >> 8);
  p.push_back(word2 & 0xFF);
  p.push_back(length >> 8);
  p.push_back(length & 0xFF);
  p.push_back(commandId);
  p.insert(p.end(), args.begin(), args.end());
  return p;
}

Satellite::Result send(Satellite& sat, CommandId id, std::vector<uint8_t> args = {}) {
  std::vector<uint8_t> raw = tcPacket(static_cast<uint8_t>(id), args);
  Telecommand::Packet tc;
  bool hasHeader;
  TEST_ASSERT_TRUE(Telecommand::parse(raw.data(), raw.size(), &tc, &hasHeader) == ErrorCode::None);
  return sat.handle(tc);
}

std::vector<uint8_t> u16(uint16_t v) { return {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v)}; }

}  // namespace

void setUp() {}
void tearDown() {}

// --- TCパケットの解析 ---

void test_parse_valid_packet() {
  std::vector<uint8_t> raw = tcPacket(6, {0x01, 0xF4}, 0x1234);
  Telecommand::Packet tc;
  bool hasHeader;
  TEST_ASSERT_TRUE(Telecommand::parse(raw.data(), raw.size(), &tc, &hasHeader) == ErrorCode::None);
  TEST_ASSERT_TRUE(hasHeader);
  TEST_ASSERT_EQUAL_HEX16(0x1234, tc.sequenceCount);
  TEST_ASSERT_EQUAL_UINT8(6, tc.commandId);
  TEST_ASSERT_EQUAL(2, tc.argsLength);
  TEST_ASSERT_EQUAL_HEX8(0xF4, tc.args[1]);
}

void test_parse_rejects_wrong_apid_and_type() {
  Telecommand::Packet tc;
  bool hasHeader;

  std::vector<uint8_t> wrongApid = tcPacket(1, {}, 3, 111);
  TEST_ASSERT_TRUE(Telecommand::parse(wrongApid.data(), wrongApid.size(), &tc, &hasHeader) ==
                   ErrorCode::MalformedPacket);
  TEST_ASSERT_TRUE(hasHeader);  // シーケンス番号は読めるので拒否のACKを返せる
  TEST_ASSERT_EQUAL(3, tc.sequenceCount);

  std::vector<uint8_t> telemetry = tcPacket(1, {});
  telemetry[0] &= ~0x10;  // type=TM
  TEST_ASSERT_TRUE(Telecommand::parse(telemetry.data(), telemetry.size(), &tc, &hasHeader) ==
                   ErrorCode::MalformedPacket);
}

void test_parse_rejects_bad_length_field() {
  std::vector<uint8_t> raw = tcPacket(1, {});
  raw.push_back(0x00);  // 長さフィールドより1バイト長い
  Telecommand::Packet tc;
  bool hasHeader;
  TEST_ASSERT_TRUE(Telecommand::parse(raw.data(), raw.size(), &tc, &hasHeader) == ErrorCode::MalformedPacket);
}

void test_parse_too_short() {
  uint8_t raw[] = {0x10, 0x6E, 0xC0};
  Telecommand::Packet tc;
  bool hasHeader;
  TEST_ASSERT_TRUE(Telecommand::parse(raw, sizeof(raw), &tc, &hasHeader) == ErrorCode::MalformedPacket);
  TEST_ASSERT_FALSE(hasHeader);
}

// --- コマンドの実行 ---

void test_initial_state() {
  Satellite sat;
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_TRUE(sat.payloadPower());
  TEST_ASSERT_EQUAL_UINT16(1000, sat.tmIntervalMs());
}

void test_no_op_is_counted() {
  Satellite sat;
  Satellite::Result r = send(sat, CommandId::NoOp);
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT16(1, sat.acceptedCommands());
  TEST_ASSERT_EQUAL_UINT8(1, sat.lastCommandId());
}

void test_unknown_command_and_bad_length() {
  Satellite sat;
  Telecommand::Packet tc;
  tc.commandId = 99;
  TEST_ASSERT_TRUE(sat.handle(tc).error == ErrorCode::UnknownCommand);

  Satellite::Result r = send(sat, CommandId::NoOp, {0x00});
  TEST_ASSERT_FALSE(r.accepted);
  TEST_ASSERT_TRUE(r.error == ErrorCode::InvalidLength);
  TEST_ASSERT_EQUAL_UINT16(2, sat.rejectedCommands());
  TEST_ASSERT_EQUAL_UINT16(0, sat.acceptedCommands());
  TEST_ASSERT_EQUAL_UINT8(0, sat.lastCommandId());  // 拒否したコマンドは「実行した」に入れない
}

void test_set_tm_interval_range() {
  Satellite sat;
  TEST_ASSERT_TRUE(send(sat, CommandId::SetTmInterval, u16(500)).accepted);
  TEST_ASSERT_EQUAL_UINT16(500, sat.tmIntervalMs());

  TEST_ASSERT_TRUE(send(sat, CommandId::SetTmInterval, u16(199)).error == ErrorCode::InvalidArgument);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetTmInterval, u16(10001)).error == ErrorCode::InvalidArgument);
  TEST_ASSERT_EQUAL_UINT16(500, sat.tmIntervalMs());  // 拒否したら変わらない
  TEST_ASSERT_TRUE(send(sat, CommandId::SetTmInterval, u16(200)).accepted);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetTmInterval, u16(10000)).accepted);
}

void test_set_mode_safe_turns_payload_off() {
  Satellite sat;
  TEST_ASSERT_TRUE(send(sat, CommandId::SetMode, {0}).accepted);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Safe);
  TEST_ASSERT_FALSE(sat.payloadPower());
  TEST_ASSERT_TRUE(send(sat, CommandId::SetMode, {3}).error == ErrorCode::InvalidArgument);
}

void test_mission_requires_payload_power() {
  Satellite sat;
  TEST_ASSERT_TRUE(send(sat, CommandId::PayloadPower, {0}).accepted);
  TEST_ASSERT_FALSE(sat.payloadPower());

  Satellite::Result r = send(sat, CommandId::SetMode, {2});
  TEST_ASSERT_TRUE(r.error == ErrorCode::NotAllowed);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);

  TEST_ASSERT_TRUE(send(sat, CommandId::PayloadPower, {1}).accepted);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetMode, {2}).accepted);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Mission);
}

void test_payload_off_during_mission_returns_to_nominal() {
  Satellite sat;
  send(sat, CommandId::SetMode, {2});
  send(sat, CommandId::PayloadPower, {0});
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_TRUE(send(sat, CommandId::PayloadPower, {2}).error == ErrorCode::InvalidArgument);
}

void test_reset_counters() {
  Satellite sat;
  send(sat, CommandId::NoOp);
  send(sat, CommandId::NoOp, {0x01});  // 拒否
  sat.countRejected();
  TEST_ASSERT_EQUAL_UINT16(2, sat.rejectedCommands());

  TEST_ASSERT_TRUE(send(sat, CommandId::ResetCounters).accepted);
  TEST_ASSERT_EQUAL_UINT16(0, sat.acceptedCommands());
  TEST_ASSERT_EQUAL_UINT16(0, sat.rejectedCommands());
  TEST_ASSERT_EQUAL_UINT8(5, sat.lastCommandId());
}

void test_beep_duration() {
  Satellite sat;
  Satellite::Result r = send(sat, CommandId::Beep, u16(300));
  TEST_ASSERT_TRUE(r.accepted);
  TEST_ASSERT_EQUAL_UINT16(300, r.beepMs);
  TEST_ASSERT_TRUE(send(sat, CommandId::Beep, u16(49)).error == ErrorCode::InvalidArgument);
  TEST_ASSERT_TRUE(send(sat, CommandId::Beep, u16(2001)).error == ErrorCode::InvalidArgument);
}

void test_fill_housekeeping() {
  Satellite sat;
  send(sat, CommandId::PayloadPower, {0});
  send(sat, CommandId::Beep, u16(1));

  HousekeepingPacket::Housekeeping hk;
  sat.fillHousekeeping(&hk);
  TEST_ASSERT_TRUE(hk.mode == OperationMode::Nominal);
  TEST_ASSERT_FALSE(hk.payloadPower);
  TEST_ASSERT_EQUAL_UINT16(1, hk.acceptedCommands);
  TEST_ASSERT_EQUAL_UINT16(1, hk.rejectedCommands);
  TEST_ASSERT_EQUAL_UINT8(4, hk.lastCommandId);
}

// --- ACKパケット ---

void test_ack_packet() {
  uint8_t buf[AckPacket::kPacketSize];
  size_t n = AckPacket::encode(0x1234, 6, AckPacket::Stage::Rejected, 3, 9, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(11, n);
  const uint8_t expected[] = {0x00, 0x66, 0xC0, 0x09, 0x00, 0x04, 0x12, 0x34, 0x06, 0x02, 0x03};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, sizeof(expected));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_valid_packet);
  RUN_TEST(test_parse_rejects_wrong_apid_and_type);
  RUN_TEST(test_parse_rejects_bad_length_field);
  RUN_TEST(test_parse_too_short);
  RUN_TEST(test_initial_state);
  RUN_TEST(test_no_op_is_counted);
  RUN_TEST(test_unknown_command_and_bad_length);
  RUN_TEST(test_set_tm_interval_range);
  RUN_TEST(test_set_mode_safe_turns_payload_off);
  RUN_TEST(test_mission_requires_payload_power);
  RUN_TEST(test_payload_off_during_mission_returns_to_nominal);
  RUN_TEST(test_reset_counters);
  RUN_TEST(test_beep_duration);
  RUN_TEST(test_fill_housekeeping);
  RUN_TEST(test_ack_packet);
  return UNITY_END();
}
