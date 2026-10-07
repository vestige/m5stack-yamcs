#pragma once

#include <EventPacket.h>
#include <stddef.h>

// 地上へ送るまでイベントを溜めておく。いっぱいになったら古いものから捨てる
class EventQueue {
 public:
  static const size_t kCapacity = 16;

  void push(const EventPacket::Event& event) {
    if (count_ == kCapacity) pop(nullptr);
    events_[(head_ + count_) % kCapacity] = event;
    count_++;
  }

  bool pop(EventPacket::Event* event) {
    if (count_ == 0) return false;
    if (event) *event = events_[head_];
    head_ = (head_ + 1) % kCapacity;
    count_--;
    return true;
  }

  size_t size() const { return count_; }

 private:
  EventPacket::Event events_[kCapacity];
  size_t head_ = 0;
  size_t count_ = 0;
};
