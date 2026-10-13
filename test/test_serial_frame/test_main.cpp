#include <SerialFrame.h>
#include <unity.h>

#include <vector>

namespace {

std::vector<uint8_t> frame(const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> out(payload.size() + SerialFrame::kOverhead);
  size_t n = SerialFrame::encode(payload.data(), payload.size(), out.data(), out.size());
  out.resize(n);
  return out;
}

std::vector<std::vector<uint8_t>> decodeAll(SerialFrame::Decoder& decoder, const std::vector<uint8_t>& stream) {
  std::vector<std::vector<uint8_t>> frames;
  for (uint8_t b : stream) {
    if (decoder.push(b)) frames.emplace_back(decoder.payload(), decoder.payload() + decoder.size());
  }
  return frames;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_crc16_ccitt_check_value() {
  // CRC-16/CCITT-FALSE の検査値: "123456789" → 0x29B1
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, SerialFrame::crc16(data, sizeof(data)));
}

void test_encode_layout() {
  std::vector<uint8_t> f = frame({0xAA, 0xBB});
  TEST_ASSERT_EQUAL(10, f.size());
  const uint8_t head[] = {0x1A, 0xCF, 0xFC, 0x1D, 0x00, 0x02, 0xAA, 0xBB};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(head, f.data(), sizeof(head));
}

void test_encode_too_large() {
  std::vector<uint8_t> payload(SerialFrame::kMaxPayload + 1);
  std::vector<uint8_t> out(payload.size() + SerialFrame::kOverhead);
  TEST_ASSERT_EQUAL(0, SerialFrame::encode(payload.data(), payload.size(), out.data(), out.size()));
}

void test_round_trip_with_noise_between_frames() {
  std::vector<uint8_t> stream = {'h', 'e', 'l', 'l', 'o', '\n', 0x1A, 0xCF};  // 起動メッセージや途中の印
  std::vector<uint8_t> a = frame({0x00, 0x64, 0xC0, 0x01});
  std::vector<uint8_t> b = frame({0x01, 0x02, 0x03});
  stream.insert(stream.end(), a.begin(), a.end());
  stream.push_back(0x55);
  stream.insert(stream.end(), b.begin(), b.end());

  SerialFrame::Decoder decoder;
  auto frames = decodeAll(decoder, stream);
  TEST_ASSERT_EQUAL(2, frames.size());
  TEST_ASSERT_EQUAL(4, frames[0].size());
  TEST_ASSERT_EQUAL_HEX8(0x64, frames[0][1]);
  TEST_ASSERT_EQUAL(3, frames[1].size());
}

void test_sync_marker_overlap() {
  // 1A 1A CF FC 1D のように、印の先頭が重なっても見つけられる
  std::vector<uint8_t> stream = {0x1A};
  std::vector<uint8_t> a = frame({0x42});
  stream.insert(stream.end(), a.begin(), a.end());
  SerialFrame::Decoder decoder;
  TEST_ASSERT_EQUAL(1, decodeAll(decoder, stream).size());
}

void test_corrupted_frame_is_dropped() {
  std::vector<uint8_t> a = frame({0x10, 0x20, 0x30});
  a[7] ^= 0xFF;  // 中身が化けた
  std::vector<uint8_t> b = frame({0x40});
  a.insert(a.end(), b.begin(), b.end());

  SerialFrame::Decoder decoder;
  auto frames = decodeAll(decoder, a);
  TEST_ASSERT_EQUAL(1, frames.size());
  TEST_ASSERT_EQUAL_HEX8(0x40, frames[0][0]);
  TEST_ASSERT_EQUAL_UINT32(1, decoder.crcErrors());
}

void test_too_long_length_resyncs() {
  std::vector<uint8_t> stream = {0x1A, 0xCF, 0xFC, 0x1D, 0xFF, 0xFF};  // 長さが大きすぎる
  std::vector<uint8_t> a = frame({0x99});
  stream.insert(stream.end(), a.begin(), a.end());
  SerialFrame::Decoder decoder;
  auto frames = decodeAll(decoder, stream);
  TEST_ASSERT_EQUAL(1, frames.size());
  TEST_ASSERT_EQUAL_HEX8(0x99, frames[0][0]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_ccitt_check_value);
  RUN_TEST(test_encode_layout);
  RUN_TEST(test_encode_too_large);
  RUN_TEST(test_round_trip_with_noise_between_frames);
  RUN_TEST(test_sync_marker_overlap);
  RUN_TEST(test_corrupted_frame_is_dropped);
  RUN_TEST(test_too_long_length_resyncs);
  return UNITY_END();
}
