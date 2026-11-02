#pragma once

#include <RecordFormat.h>
#include <stddef.h>
#include <stdint.h>

// データレコーダ (#55): 送るテレメトリを、M5Stack の microSD に機器ごとに記録する。
// パスの外 (地上に届かない間) のデータも残り、あとのダンプ (#56) で降ろせる。
// 書き込みの回数を減らすため、機器ごとに貯めて kFlushIntervalMs ごとにまとめて書く。
// microSD が無い・壊れたときも衛星は動き続け、記録だけ止める (状態は HK とイベントで知らせる)
class DataRecorder {
 public:
  enum class State : uint8_t {
    NoCard = 0,       // microSD が無い (読めない)
    Recording = 1,    // 記録している
    WaitingTime = 2,  // 機上時刻がわからないので、まだ記録していない
    Error = 3,        // 書き込みに失敗した
  };

  void begin();
  // 送るパケットを記録する (貯めておき、poll で書く)
  void record(const uint8_t* packet, size_t size);
  // まとめて書く、空きを確かめる、microSD をつなぎ直す
  void poll(uint32_t nowMs);

  State state() const { return state_; }
  uint16_t freeMB() const { return freeMB_; }
  uint32_t records() const { return records_; }

 private:
  static const size_t kBufferSize = 4096;
  static const uint32_t kFlushIntervalMs = 5000;
  static const uint32_t kSpaceCheckIntervalMs = 60000;
  static const uint32_t kRetryIntervalMs = 10000;
  // 空きがこれより少なくなったら、古い記録から消す
  static const uint16_t kMinFreeMB = 100;

  struct Buffer {
    uint8_t data[kBufferSize];
    size_t used = 0;
    char path[RecordFormat::kMaxPathLength] = "";
  };

  bool mount();
  bool flush(Buffer& buffer);
  void flushAll();
  void updateFreeSpace();
  bool removeOldest(RecordFormat::Source source);
  void printContents();

  Buffer buffers_[RecordFormat::kSourceCount];
  State state_ = State::NoCard;
  bool mounted_ = false;
  uint16_t freeMB_ = 0;
  uint32_t records_ = 0;
  uint32_t lastFlushMs_ = 0;
  uint32_t lastSpaceCheckMs_ = 0;
  uint32_t lastRetryMs_ = 0;
};
