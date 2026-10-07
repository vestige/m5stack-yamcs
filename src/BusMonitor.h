#pragma once

#include <HousekeepingPacket.h>
#include <Satellite.h>

#include "GroundLink.h"

// バスの健康状態 (HK) を集める
class BusMonitor {
 public:
  // 再起動の回数を数え、前回の再起動の理由を読む
  void begin();

  uint16_t bootCount() const { return bootCount_; }
  uint8_t resetReason() const { return resetReason_; }

  HousekeepingPacket::Housekeeping collect(const Satellite& satellite, const GroundLink& link) const;

 private:
  uint16_t bootCount_ = 0;
  uint8_t resetReason_ = 0;
};
