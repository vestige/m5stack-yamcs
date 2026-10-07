#pragma once

#include <stdint.h>

// 一定の周期ごとに true を返す
class IntervalTimer {
 public:
  bool elapsed(uint32_t now, uint32_t intervalMs) {
    if (now - last_ < intervalMs) return false;
    last_ = now;
    return true;
  }

 private:
  uint32_t last_ = 0;
};
