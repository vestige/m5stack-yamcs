#pragma once

#include <Arduino.h>

// ペイロードの電源を表すLED: ピン ── 抵抗(220〜330Ω) ── LED ── GND
class PayloadLed {
 public:
  explicit PayloadLed(int pin) : pin_(pin) {}

  void begin(bool on) {
    pinMode(pin_, OUTPUT);
    show(on);
  }

  void show(bool on) { digitalWrite(pin_, on ? HIGH : LOW); }

 private:
  int pin_;
};
