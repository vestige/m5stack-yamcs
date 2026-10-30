#include <AckPacket.h>
#include <EventPacket.h>
#include <ObservationPacket.h>
#include <Satellite.h>
#include <Telecommand.h>
#include <unity.h>

#include <vector>

using Telecommand::CommandId;
using Telecommand::ErrorCode;
using EventPacket::EventId;
using EventPacket::Severity;
using OperationMode = Satellite::OperationMode;

namespace {

// パケットの副ヘッダの機上時刻。バイトの並びがわかりやすい値にしておく
const uint64_t kPacketTimeMs = 0x0102030405060708ULL;

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
  sat.reject(0, ErrorCode::MalformedPacket);
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

// --- イベント ---

namespace {

std::vector<EventPacket::Event> drainEvents(Satellite& sat) {
  std::vector<EventPacket::Event> events;
  EventPacket::Event e;
  while (sat.popEvent(&e)) events.push_back(e);
  return events;
}

}  // namespace

void test_no_events_initially_and_for_no_op() {
  Satellite sat;
  send(sat, CommandId::NoOp);
  send(sat, CommandId::Beep, u16(300));
  TEST_ASSERT_EQUAL(0, drainEvents(sat).size());
}

void test_safe_mode_reports_mode_and_payload_events() {
  Satellite sat;
  send(sat, CommandId::SetMode, {0});
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_EQUAL(2, events.size());
  TEST_ASSERT_TRUE(events[0].id == EventId::ModeChanged);
  TEST_ASSERT_EQUAL_UINT16(1, events[0].arg1);  // NOMINAL
  TEST_ASSERT_EQUAL_UINT16(0, events[0].arg2);  // SAFE
  TEST_ASSERT_TRUE(events[1].id == EventId::PayloadPowerChanged);
  TEST_ASSERT_EQUAL_UINT16(0, events[1].arg1);
  TEST_ASSERT_TRUE(events[1].severity == Severity::Info);
}

void test_unchanged_state_reports_nothing() {
  Satellite sat;
  send(sat, CommandId::SetMode, {1});          // すでに NOMINAL
  send(sat, CommandId::PayloadPower, {1});     // すでに ON
  TEST_ASSERT_EQUAL(0, drainEvents(sat).size());
}

void test_payload_off_during_mission_reports_both() {
  Satellite sat;
  send(sat, CommandId::SetMode, {2});
  drainEvents(sat);
  send(sat, CommandId::PayloadPower, {0});
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_EQUAL(2, events.size());
  TEST_ASSERT_TRUE(events[0].id == EventId::PayloadPowerChanged);
  TEST_ASSERT_TRUE(events[1].id == EventId::ModeChanged);
  TEST_ASSERT_EQUAL_UINT16(2, events[1].arg1);  // MISSION
  TEST_ASSERT_EQUAL_UINT16(1, events[1].arg2);  // NOMINAL
}

void test_tm_interval_and_counter_reset_events() {
  Satellite sat;
  send(sat, CommandId::SetTmInterval, u16(2000));
  send(sat, CommandId::ResetCounters);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_EQUAL(2, events.size());
  TEST_ASSERT_TRUE(events[0].id == EventId::TmIntervalChanged);
  TEST_ASSERT_EQUAL_UINT16(2000, events[0].arg1);
  TEST_ASSERT_TRUE(events[1].id == EventId::CountersReset);
}

void test_rejected_command_reports_warning_with_reason() {
  Satellite sat;
  send(sat, CommandId::SetTmInterval, u16(50));
  sat.reject(0, ErrorCode::MalformedPacket);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_EQUAL(2, events.size());
  TEST_ASSERT_TRUE(events[0].severity == Severity::Warning);
  TEST_ASSERT_TRUE(events[0].id == EventId::CommandRejected);
  TEST_ASSERT_EQUAL_UINT16(3, events[0].arg1);  // SET_TM_INTERVAL
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(ErrorCode::InvalidArgument), events[0].arg2);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(ErrorCode::MalformedPacket), events[1].arg2);
}

void test_event_queue_drops_oldest_when_full() {
  EventQueue queue;
  for (uint16_t i = 0; i < EventQueue::kCapacity + 3; i++) {
    EventPacket::Event e;
    e.arg1 = i;
    queue.push(e);
  }
  TEST_ASSERT_EQUAL(EventQueue::kCapacity, queue.size());
  EventPacket::Event e;
  TEST_ASSERT_TRUE(queue.pop(&e));
  TEST_ASSERT_EQUAL_UINT16(3, e.arg1);
}

void test_event_packet() {
  EventPacket::Event e;
  e.severity = Severity::Warning;
  e.id = EventId::CommandRejected;
  e.arg1 = 3;
  e.arg2 = 0x0102;
  uint8_t buf[EventPacket::kPacketSize];
  TEST_ASSERT_EQUAL(20, EventPacket::encode(e, 5, kPacketTimeMs, buf, sizeof(buf)));
  const uint8_t expected[] = {0x08, 0x67, 0xC0, 0x05, 0x00, 0x0D, 0x01, 0x02, 0x03, 0x04,
                              0x05, 0x06, 0x07, 0x08, 0x02, 0x06, 0x00, 0x03, 0x01, 0x02};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, sizeof(expected));
}

// --- FDIR ---

namespace {

const uint32_t kTimeout = Satellite::kGnssTimeoutMs;
const uint32_t kStartup = Satellite::kGnssStartupTimeoutMs;

}  // namespace

void test_fdir_waits_grace_period_after_payload_on() {
  Satellite sat;
  sat.checkHealth(1000, 0);              // 監視開始 (まだデータなし)
  sat.checkHealth(1000 + kStartup - 1, 0);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_TRUE(sat.payloadPower());
  sat.checkHealth(1000 + kStartup, 0);   // 最初のデータが30秒届かない → 異常
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Safe);
  TEST_ASSERT_EQUAL_UINT16(30, drainEvents(sat)[0].arg2);
}

void test_fdir_uses_short_timeout_after_first_data() {
  Satellite sat;
  sat.checkHealth(1000, 0);
  sat.checkHealth(3000, 2500);                 // 最初のデータが届いた
  sat.checkHealth(2500 + kTimeout - 1, 2500);
  TEST_ASSERT_TRUE(sat.payloadPower());
  sat.checkHealth(2500 + kTimeout, 2500);      // その後は10秒で判断する
  TEST_ASSERT_FALSE(sat.payloadPower());
}

void test_fdir_no_fault_while_data_arrives() {
  Satellite sat;
  sat.checkHealth(1000, 0);
  for (uint32_t t = 2000; t < 60000; t += 1000) sat.checkHealth(t, t - 500);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_TRUE(sat.lastFault() == Satellite::Fault::None);
}

void test_fdir_detects_gnss_timeout_and_goes_safe() {
  Satellite sat;
  sat.checkHealth(1000, 0);
  sat.checkHealth(5000, 4000);                 // データが届いている
  drainEvents(sat);
  sat.checkHealth(4000 + kTimeout - 1, 4000);  // まだ待つ
  TEST_ASSERT_TRUE(sat.payloadPower());
  sat.checkHealth(4000 + kTimeout, 4000);      // 10秒届かない → 異常

  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Safe);
  TEST_ASSERT_FALSE(sat.payloadPower());
  TEST_ASSERT_TRUE(sat.lastFault() == Satellite::Fault::GnssNoData);
  TEST_ASSERT_EQUAL_UINT16(1, sat.faultCount());

  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_EQUAL(3, events.size());
  TEST_ASSERT_TRUE(events[0].id == EventId::FaultDetected);
  TEST_ASSERT_TRUE(events[0].severity == Severity::Distress);
  TEST_ASSERT_EQUAL_UINT16(1, events[0].arg1);   // GNSS_NO_DATA
  TEST_ASSERT_EQUAL_UINT16(10, events[0].arg2);  // 10秒
  TEST_ASSERT_TRUE(events[1].id == EventId::PayloadPowerChanged);
  TEST_ASSERT_TRUE(events[2].id == EventId::ModeChanged);
  TEST_ASSERT_EQUAL_UINT16(0, events[2].arg2);   // SAFE
}

void test_fdir_old_data_does_not_count_after_power_on() {
  Satellite sat;
  sat.checkHealth(1000, 0);
  sat.checkHealth(1000 + kStartup, 0);  // 異常 → ペイロードOFF
  TEST_ASSERT_FALSE(sat.payloadPower());

  // ケーブルを抜いたまま電源を入れ直す: 電源を入れる前の古いデータは数えない
  send(sat, CommandId::PayloadPower, {1});
  uint32_t on = 50000;
  sat.checkHealth(on, 900);
  sat.checkHealth(on + kStartup - 1, 900);
  TEST_ASSERT_TRUE(sat.payloadPower());
  sat.checkHealth(on + kStartup, 900);
  TEST_ASSERT_FALSE(sat.payloadPower());
  TEST_ASSERT_EQUAL_UINT16(2, sat.faultCount());
}

void test_fdir_does_nothing_while_payload_off() {
  Satellite sat;
  send(sat, CommandId::PayloadPower, {0});
  sat.checkHealth(1000, 0);
  sat.checkHealth(1000 + kTimeout * 3, 0);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_EQUAL_UINT16(0, sat.faultCount());
}

void test_fdir_disabled_does_not_go_safe() {
  Satellite sat;
  TEST_ASSERT_TRUE(send(sat, CommandId::SetFdir, {0}).accepted);
  TEST_ASSERT_FALSE(sat.fdirEnabled());
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_EQUAL(1, events.size());
  TEST_ASSERT_TRUE(events[0].id == EventId::FdirChanged);
  TEST_ASSERT_EQUAL_UINT16(0, events[0].arg1);

  sat.checkHealth(1000, 0);
  sat.checkHealth(1000 + kTimeout * 3, 0);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_TRUE(sat.payloadPower());
}

void test_fdir_reenabled_restarts_grace_period() {
  Satellite sat;
  send(sat, CommandId::SetFdir, {0});
  sat.checkHealth(1000, 0);
  sat.checkHealth(30000, 0);
  send(sat, CommandId::SetFdir, {1});
  sat.checkHealth(30001, 0);               // 有効にした時点から監視し直す
  sat.checkHealth(30001 + kStartup - 1, 0);
  TEST_ASSERT_TRUE(sat.payloadPower());
  sat.checkHealth(30001 + kStartup, 0);
  TEST_ASSERT_FALSE(sat.payloadPower());
}

void test_set_fdir_argument_range() {
  Satellite sat;
  TEST_ASSERT_TRUE(send(sat, CommandId::SetFdir, {2}).error == ErrorCode::InvalidArgument);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetFdir, {1}).accepted);  // すでに有効: 何も起きない
  sat.reject(0, ErrorCode::None);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_EQUAL(2, events.size());  // 拒否2つ (FDIR_CHANGED は出ない)
}

void test_fill_housekeeping_fdir() {
  Satellite sat;
  sat.checkHealth(1000, 0);
  sat.checkHealth(1000 + kStartup, 0);
  HousekeepingPacket::Housekeeping hk;
  sat.fillHousekeeping(&hk);
  TEST_ASSERT_TRUE(hk.fdirEnabled);
  TEST_ASSERT_TRUE(hk.lastFault == Satellite::Fault::GnssNoData);
  TEST_ASSERT_EQUAL_UINT16(1, hk.faultCount);
}

// --- 機上時刻と観測 ---

namespace {

const uint32_t kUtc = 1791166425;  // 2026-10-05T02:13:45Z

std::vector<uint8_t> scheduleArgs(uint32_t start, uint16_t duration) {
  return {static_cast<uint8_t>(start >> 24), static_cast<uint8_t>(start >> 16), static_cast<uint8_t>(start >> 8),
          static_cast<uint8_t>(start), static_cast<uint8_t>(duration >> 8), static_cast<uint8_t>(duration)};
}

// 機上時刻を kUtc に合わせ (nowMs=1000)、ペイロードのデータが届いている状態にする
Satellite syncedSatellite() {
  Satellite sat;
  sat.syncTime(kUtc, 1000);
  sat.checkHealth(1000, 1000);
  sat.updateObservation(1000);
  return sat;
}

// nowMs まで時間を進める (GNSSのデータは届き続ける)
void advance(Satellite& sat, uint32_t nowMs) {
  sat.checkHealth(nowMs, nowMs);
  sat.updateObservation(nowMs);
}

}  // namespace

void test_onboard_clock_millis() {
  OnboardClock clock;
  TEST_ASSERT_TRUE(clock.nowMillis(5000) == 0);  // 合わせる前は0
  clock.sync(kUtc, 2000);
  TEST_ASSERT_TRUE(clock.nowMillis(2000) == static_cast<uint64_t>(kUtc) * 1000);
  TEST_ASSERT_TRUE(clock.nowMillis(2345) == static_cast<uint64_t>(kUtc) * 1000 + 345);
}

void test_onboard_clock() {
  OnboardClock clock;
  TEST_ASSERT_FALSE(clock.valid());
  TEST_ASSERT_EQUAL_UINT32(0, clock.now(5000));
  clock.sync(kUtc, 2000);
  TEST_ASSERT_EQUAL_UINT32(kUtc, clock.now(2000));
  TEST_ASSERT_EQUAL_UINT32(kUtc + 61, clock.now(63500));  // GPSの時刻が来なくても数え続ける
}

void test_schedule_requires_onboard_time() {
  Satellite sat;
  Satellite::Result r = send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 60, 60));
  TEST_ASSERT_TRUE(r.error == ErrorCode::NotAllowed);
}

void test_schedule_argument_checks() {
  Satellite sat = syncedSatellite();
  TEST_ASSERT_TRUE(send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc, 60)).error ==
                   ErrorCode::InvalidArgument);  // 過去 (今と同じ) の時刻
  TEST_ASSERT_TRUE(send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 60, 9)).error ==
                   ErrorCode::InvalidArgument);
  TEST_ASSERT_TRUE(send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 60, 3601)).error ==
                   ErrorCode::InvalidArgument);
  TEST_ASSERT_TRUE(send(sat, CommandId::ScheduleObservation, {0, 0, 0}).error == ErrorCode::InvalidLength);
  TEST_ASSERT_TRUE(send(sat, CommandId::CancelObservation).error == ErrorCode::NotAllowed);  // 予約がない
}

void test_observation_runs_on_schedule() {
  Satellite sat = syncedSatellite();
  drainEvents(sat);
  TEST_ASSERT_TRUE(send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 300, 180)).accepted);
  TEST_ASSERT_TRUE(sat.observationState() == Satellite::ObservationState::Scheduled);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_TRUE(events[0].id == EventId::ObservationScheduled);
  TEST_ASSERT_EQUAL_UINT16(300, events[0].arg1);
  TEST_ASSERT_EQUAL_UINT16(180, events[0].arg2);

  advance(sat, 1000 + 299000);  // 開始の1秒前
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  advance(sat, 1000 + 300000);  // 開始
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Mission);
  TEST_ASSERT_TRUE(sat.observationState() == Satellite::ObservationState::Active);
  events = drainEvents(sat);
  TEST_ASSERT_TRUE(events[0].id == EventId::ModeChanged);
  TEST_ASSERT_TRUE(events[1].id == EventId::ObservationStarted);

  advance(sat, 1000 + 479000);  // 終了の1秒前
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Mission);
  advance(sat, 1000 + 480000);  // 終了
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_TRUE(sat.observationState() == Satellite::ObservationState::None);
  events = drainEvents(sat);
  TEST_ASSERT_TRUE(events[1].id == EventId::ObservationEnded);
  TEST_ASSERT_EQUAL_UINT16(0, events[1].arg1);  // 完了
}

void test_observation_skipped_when_payload_off() {
  Satellite sat = syncedSatellite();
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 60, 60));
  send(sat, CommandId::PayloadPower, {0});
  drainEvents(sat);
  sat.updateObservation(1000 + 60000);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  TEST_ASSERT_TRUE(sat.observationState() == Satellite::ObservationState::None);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_TRUE(events[0].id == EventId::ObservationSkipped);
  TEST_ASSERT_TRUE(events[0].severity == Severity::Warning);
  TEST_ASSERT_EQUAL_UINT16(Satellite::kSkippedPayloadOff, events[0].arg1);
}

void test_observation_interrupted_by_fdir() {
  Satellite sat = syncedSatellite();
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 10, 600));
  advance(sat, 1000 + 10000);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Mission);
  drainEvents(sat);

  // GNSSのデータが止まる → FDIR が SAFE へ退避 → 観測は中断
  sat.checkHealth(1000 + 10000 + Satellite::kGnssTimeoutMs, 1000 + 10000);
  sat.updateObservation(1000 + 10000 + Satellite::kGnssTimeoutMs);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Safe);
  TEST_ASSERT_TRUE(sat.observationState() == Satellite::ObservationState::None);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_TRUE(events.back().id == EventId::ObservationEnded);
  TEST_ASSERT_EQUAL_UINT16(1, events.back().arg1);  // 中断
}

void test_cancel_observation() {
  Satellite sat = syncedSatellite();
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 60, 60));
  TEST_ASSERT_TRUE(send(sat, CommandId::CancelObservation).accepted);
  advance(sat, 1000 + 61000);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);  // 取り消したので観測しない

  // 観測中に取り消すと NOMINAL に戻る
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 120, 60));
  advance(sat, 1000 + 120000);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Mission);
  TEST_ASSERT_TRUE(send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 500, 60)).error ==
                   ErrorCode::NotAllowed);  // 観測中は予約し直せない
  drainEvents(sat);
  TEST_ASSERT_TRUE(send(sat, CommandId::CancelObservation).accepted);
  TEST_ASSERT_TRUE(sat.mode() == OperationMode::Nominal);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_TRUE(events.back().id == EventId::ObservationCancelled);
  TEST_ASSERT_EQUAL_UINT16(1, events.back().arg1);
}

void test_fill_housekeeping_observation() {
  Satellite sat = syncedSatellite();
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 300, 180));
  sat.updateObservation(6000);
  HousekeepingPacket::Housekeeping hk;
  sat.fillHousekeeping(&hk);
  TEST_ASSERT_EQUAL_UINT32(kUtc + 5, hk.onboardTime);
  TEST_ASSERT_TRUE(hk.observationState == Satellite::ObservationState::Scheduled);
  TEST_ASSERT_EQUAL_UINT32(kUtc + 300, hk.observationStart);
  TEST_ASSERT_EQUAL_UINT16(180, hk.observationDuration);
}

// 観測の結果は、通信できない間に観測しても次のパスでわかるよう、HKに残る
HousekeepingPacket::Housekeeping housekeeping(const Satellite& sat) {
  HousekeepingPacket::Housekeeping hk;
  sat.fillHousekeeping(&hk);
  return hk;
}

void test_last_observation_completed() {
  Satellite sat = syncedSatellite();
  TEST_ASSERT_TRUE(housekeeping(sat).lastObservationResult == Satellite::ObservationResult::None);
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 300, 180));
  advance(sat, 1000 + 302000);  // 2秒遅れて始まった
  advance(sat, 1000 + 482000);
  HousekeepingPacket::Housekeeping hk = housekeeping(sat);
  TEST_ASSERT_TRUE(hk.lastObservationResult == Satellite::ObservationResult::Completed);
  TEST_ASSERT_EQUAL_UINT32(kUtc + 302, hk.lastObservationStart);  // 実際に始めた時刻
  TEST_ASSERT_EQUAL_UINT16(180, hk.lastObservationLength);
  TEST_ASSERT_EQUAL_UINT16(1, hk.completedObservations);
  TEST_ASSERT_TRUE(hk.observationState == Satellite::ObservationState::None);  // 観測の状態は戻る
}

void test_last_observation_skipped() {
  Satellite sat = syncedSatellite();
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 60, 60));
  send(sat, CommandId::PayloadPower, {0});
  sat.updateObservation(1000 + 60000);
  HousekeepingPacket::Housekeeping hk = housekeeping(sat);
  TEST_ASSERT_TRUE(hk.lastObservationResult == Satellite::ObservationResult::SkippedPayloadOff);
  TEST_ASSERT_EQUAL_UINT32(kUtc + 60, hk.lastObservationStart);  // 予約の時刻
  TEST_ASSERT_EQUAL_UINT16(0, hk.lastObservationLength);
  TEST_ASSERT_EQUAL_UINT16(0, hk.completedObservations);

  send(sat, CommandId::PayloadPower, {1});
  send(sat, CommandId::SetMode, {2});  // MISSION
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 120, 60));
  advance(sat, 1000 + 120000);
  TEST_ASSERT_TRUE(housekeeping(sat).lastObservationResult == Satellite::ObservationResult::SkippedNotNominal);
}

void test_last_observation_aborted_and_cancelled() {
  Satellite sat = syncedSatellite();
  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 10, 600));
  advance(sat, 1000 + 10000);
  send(sat, CommandId::SetMode, {1});  // 観測中に NOMINAL へ戻す
  advance(sat, 1000 + 40000);
  HousekeepingPacket::Housekeeping hk = housekeeping(sat);
  TEST_ASSERT_TRUE(hk.lastObservationResult == Satellite::ObservationResult::Aborted);
  TEST_ASSERT_EQUAL_UINT32(kUtc + 10, hk.lastObservationStart);
  TEST_ASSERT_EQUAL_UINT16(30, hk.lastObservationLength);

  send(sat, CommandId::ScheduleObservation, scheduleArgs(kUtc + 100, 60));
  advance(sat, 1000 + 100000);
  advance(sat, 1000 + 120000);
  send(sat, CommandId::CancelObservation);  // 観測中に取り消す
  hk = housekeeping(sat);
  TEST_ASSERT_TRUE(hk.lastObservationResult == Satellite::ObservationResult::Cancelled);
  TEST_ASSERT_EQUAL_UINT32(kUtc + 100, hk.lastObservationStart);
  TEST_ASSERT_EQUAL_UINT16(20, hk.lastObservationLength);
  TEST_ASSERT_EQUAL_UINT16(0, hk.completedObservations);
}

// --- ミッション要求のランプ ---

std::vector<uint8_t> lampArgs(Satellite::LampColor color, uint16_t duration) {
  return {static_cast<uint8_t>(color), static_cast<uint8_t>(duration >> 8), static_cast<uint8_t>(duration)};
}

void test_lamp_on_and_expire() {
  Satellite sat;
  sat.updateLamp(1000);
  drainEvents(sat);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Cyan, 60)).accepted);
  TEST_ASSERT_TRUE(sat.lampColor() == Satellite::LampColor::Cyan);
  TEST_ASSERT_EQUAL_UINT16(60, sat.lampRemainingSec(1000));
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_TRUE(events[0].id == EventId::LampOn);
  TEST_ASSERT_EQUAL_UINT16(5, events[0].arg1);  // CYAN
  TEST_ASSERT_EQUAL_UINT16(60, events[0].arg2);

  sat.updateLamp(1000 + 59500);
  TEST_ASSERT_EQUAL_UINT16(1, sat.lampRemainingSec(1000 + 59500));  // 残りは切り上げ
  sat.updateLamp(1000 + 60000);  // 時間が来た
  TEST_ASSERT_TRUE(sat.lampColor() == Satellite::LampColor::Off);
  TEST_ASSERT_EQUAL_UINT16(0, sat.lampRemainingSec(1000 + 60000));
  events = drainEvents(sat);
  TEST_ASSERT_TRUE(events[0].id == EventId::LampOff);
  TEST_ASSERT_EQUAL_UINT16(Satellite::kLampOffExpired, events[0].arg2);

  HousekeepingPacket::Housekeeping hk;
  sat.fillHousekeeping(&hk);
  TEST_ASSERT_TRUE(hk.lampColor == Satellite::LampColor::Off);
}

void test_lamp_argument_checks() {
  Satellite sat;
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Red, 0)).error ==
                   ErrorCode::InvalidArgument);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Red, 601)).error ==
                   ErrorCode::InvalidArgument);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, {8, 0, 10}).error == ErrorCode::InvalidArgument);  // 知らない色
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, {1, 0}).error == ErrorCode::InvalidLength);
  // 消すのは時間を問わない
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Off, 0)).accepted);
}

void test_payload_on_time() {
  Satellite sat;
  sat.updateLamp(5000);  // 起動から5秒 (ペイロードは起動時から ON)
  TEST_ASSERT_EQUAL_UINT16(5, sat.payloadOnSec(5000));
  send(sat, CommandId::PayloadPower, {0});
  TEST_ASSERT_EQUAL_UINT16(0, sat.payloadOnSec(9000));
  sat.updateLamp(10000);
  send(sat, CommandId::PayloadPower, {1});  // 10秒に入れ直す
  TEST_ASSERT_EQUAL_UINT16(0, sat.payloadOnSec(10000));
  TEST_ASSERT_EQUAL_UINT16(29, sat.payloadOnSec(39999));
  TEST_ASSERT_EQUAL_UINT16(30, sat.payloadOnSec(40000));
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, sat.payloadOnSec(10000 + 70000000));  // 頭打ち
  HousekeepingPacket::Housekeeping hk;
  sat.updateLamp(25000);
  sat.fillHousekeeping(&hk);
  TEST_ASSERT_EQUAL_UINT16(15, hk.payloadOnTime);
}

void test_lamp_needs_payload_power() {
  Satellite sat;
  send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Yellow, 300));
  drainEvents(sat);
  // ペイロードの電源を切るとランプも消え、電源が切れている間は点けられない
  send(sat, CommandId::PayloadPower, {0});
  TEST_ASSERT_TRUE(sat.lampColor() == Satellite::LampColor::Off);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  bool found = false;
  for (const auto& e : events) {
    if (e.id == EventId::LampOff) {
      found = true;
      TEST_ASSERT_EQUAL_UINT16(Satellite::kLampOffPayloadOff, e.arg2);
    }
  }
  TEST_ASSERT_TRUE(found);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Yellow, 300)).error ==
                   ErrorCode::NotAllowed);
  send(sat, CommandId::PayloadPower, {1});
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Yellow, 300)).accepted);
}

void test_lamp_off_by_command_and_safe() {
  Satellite sat;
  send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Green, 300));
  drainEvents(sat);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Off, 0)).accepted);
  TEST_ASSERT_TRUE(sat.lampColor() == Satellite::LampColor::Off);
  std::vector<EventPacket::Event> events = drainEvents(sat);
  TEST_ASSERT_TRUE(events[0].id == EventId::LampOff);
  TEST_ASSERT_EQUAL_UINT16(2, events[0].arg1);  // 消す前は GREEN
  TEST_ASSERT_EQUAL_UINT16(Satellite::kLampOffByCommand, events[0].arg2);

  // SAFE になったら消え、SAFE の間は点けられない
  send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Blue, 300));
  send(sat, CommandId::SetMode, {0});
  TEST_ASSERT_TRUE(sat.lampColor() == Satellite::LampColor::Off);
  TEST_ASSERT_TRUE(send(sat, CommandId::SetLamp, lampArgs(Satellite::LampColor::Blue, 300)).error ==
                   ErrorCode::NotAllowed);
}

void test_observation_packet() {
  SatelliteInfo sats[3];
  sats[0].system = GnssSystem::QZSS;
  sats[0].prn = 2;
  sats[0].elevation = 80;
  sats[0].azimuth = 180;
  sats[0].snr = 45;
  sats[1].system = GnssSystem::GPS;
  sats[1].prn = 5;
  sats[1].elevation = 30;
  sats[1].azimuth = 270;
  sats[1].snr = 36;
  sats[2].system = GnssSystem::GPS;
  sats[2].prn = 7;  // 仰角・方位角は不明、信号なし

  uint8_t buf[ObservationPacket::kMaxPacketSize];
  size_t n = ObservationPacket::encode(kUtc, sats, 3, 4, kPacketTimeMs, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(27 + 18, n);
  TEST_ASSERT_EQUAL_HEX8(0x68, buf[1]);  // APID 104
  TEST_ASSERT_EQUAL_UINT8(3, buf[18]);   // 見えている数
  TEST_ASSERT_EQUAL_UINT8(2, buf[19]);   // 捕捉している数
  TEST_ASSERT_EQUAL_UINT8(45, buf[20]);  // 最大
  TEST_ASSERT_EQUAL_UINT8(41, buf[21]);  // 平均 (45+36)/2=40.5 → 41
  const uint8_t perSystem[] = {1, 0, 0, 0, 1};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(perSystem, buf + 22, 5);
  const uint8_t last[] = {0, 7, 0xFF, 0xFF, 0xFF, 0};  // GPS, PRN7, 仰角-1, 方位角不明, C/N0 0
  TEST_ASSERT_EQUAL_HEX8_ARRAY(last, buf + 27 + 12, 6);
}

void test_observation_packet_limits_satellites() {
  SatelliteInfo sats[40];
  uint8_t buf[ObservationPacket::kMaxPacketSize];
  TEST_ASSERT_EQUAL(ObservationPacket::kMaxPacketSize, ObservationPacket::encode(kUtc, sats, 40, 0, kPacketTimeMs, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT8(32, buf[18]);
  TEST_ASSERT_EQUAL(0, ObservationPacket::encode(kUtc, sats, 40, 0, kPacketTimeMs, buf, sizeof(buf) - 1));
}

// --- ACKパケット ---

void test_ack_packet() {
  uint8_t buf[AckPacket::kPacketSize];
  size_t n = AckPacket::encode(0x1234, 6, AckPacket::Stage::Rejected, 3, 9, kPacketTimeMs, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(19, n);
  const uint8_t expected[] = {0x08, 0x66, 0xC0, 0x09, 0x00, 0x0C, 0x01, 0x02, 0x03, 0x04,
                              0x05, 0x06, 0x07, 0x08, 0x12, 0x34, 0x06, 0x02, 0x03};
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
  RUN_TEST(test_no_events_initially_and_for_no_op);
  RUN_TEST(test_safe_mode_reports_mode_and_payload_events);
  RUN_TEST(test_unchanged_state_reports_nothing);
  RUN_TEST(test_payload_off_during_mission_reports_both);
  RUN_TEST(test_tm_interval_and_counter_reset_events);
  RUN_TEST(test_rejected_command_reports_warning_with_reason);
  RUN_TEST(test_event_queue_drops_oldest_when_full);
  RUN_TEST(test_event_packet);
  RUN_TEST(test_fdir_waits_grace_period_after_payload_on);
  RUN_TEST(test_fdir_uses_short_timeout_after_first_data);
  RUN_TEST(test_fdir_no_fault_while_data_arrives);
  RUN_TEST(test_fdir_detects_gnss_timeout_and_goes_safe);
  RUN_TEST(test_fdir_old_data_does_not_count_after_power_on);
  RUN_TEST(test_fdir_does_nothing_while_payload_off);
  RUN_TEST(test_fdir_disabled_does_not_go_safe);
  RUN_TEST(test_fdir_reenabled_restarts_grace_period);
  RUN_TEST(test_set_fdir_argument_range);
  RUN_TEST(test_fill_housekeeping_fdir);
  RUN_TEST(test_onboard_clock);
  RUN_TEST(test_onboard_clock_millis);
  RUN_TEST(test_schedule_requires_onboard_time);
  RUN_TEST(test_schedule_argument_checks);
  RUN_TEST(test_observation_runs_on_schedule);
  RUN_TEST(test_observation_skipped_when_payload_off);
  RUN_TEST(test_observation_interrupted_by_fdir);
  RUN_TEST(test_cancel_observation);
  RUN_TEST(test_fill_housekeeping_observation);
  RUN_TEST(test_last_observation_completed);
  RUN_TEST(test_last_observation_skipped);
  RUN_TEST(test_last_observation_aborted_and_cancelled);
  RUN_TEST(test_lamp_on_and_expire);
  RUN_TEST(test_lamp_argument_checks);
  RUN_TEST(test_lamp_off_by_command_and_safe);
  RUN_TEST(test_lamp_needs_payload_power);
  RUN_TEST(test_payload_on_time);
  RUN_TEST(test_observation_packet);
  RUN_TEST(test_observation_packet_limits_satellites);
  RUN_TEST(test_ack_packet);
  return UNITY_END();
}
