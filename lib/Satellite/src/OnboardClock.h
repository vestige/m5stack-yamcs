#pragma once

#include <stdint.h>

// 機上時刻: GPSの時刻で合わせ、その後は衛星の中の時計 (millis) で数え続ける。
// ペイロードを切ってGPSの時刻が来なくなっても、時刻はわかる
class OnboardClock {
 public:
  // GPSの時刻 utcSec を、衛星の中の時計の nowMs に合わせる
  void sync(uint32_t utcSec, uint32_t nowMs) {
    utcAtSync_ = utcSec;
    msAtSync_ = nowMs;
    valid_ = true;
  }

  bool valid() const { return valid_; }

  // 今の時刻 (Unix秒)。まだ一度も合わせていなければ0
  uint32_t now(uint32_t nowMs) const { return valid_ ? utcAtSync_ + (nowMs - msAtSync_) / 1000 : 0; }
  // 今の時刻 (Unix 時刻のミリ秒)。テレメトリのパケットの時刻に使う。まだ一度も合わせていなければ0
  uint64_t nowMillis(uint32_t nowMs) const {
    return valid_ ? static_cast<uint64_t>(utcAtSync_) * 1000 + (nowMs - msAtSync_) : 0;
  }

 private:
  bool valid_ = false;
  uint32_t utcAtSync_ = 0;
  uint32_t msAtSync_ = 0;
};
