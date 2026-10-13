#pragma once

#include <Preferences.h>

// 地上局との回線の選択。電源を切っても消えないよう NVS (フラッシュ) に保存する
class LinkSetting {
 public:
  enum class Link : uint8_t { Udp = 0, EspNow = 1 };

  // 保存された回線を読む。まだ保存されていなければ defaultLink
  static Link load(Link defaultLink) {
    Preferences prefs;
    prefs.begin("m5sat", true);
    uint8_t value = prefs.getUChar("link", static_cast<uint8_t>(defaultLink));
    prefs.end();
    return value == static_cast<uint8_t>(Link::EspNow) ? Link::EspNow : Link::Udp;
  }

  static void save(Link link) {
    Preferences prefs;
    prefs.begin("m5sat", false);
    prefs.putUChar("link", static_cast<uint8_t>(link));
    prefs.end();
  }

  static Link other(Link link) { return link == Link::Udp ? Link::EspNow : Link::Udp; }
  static const char* name(Link link) { return link == Link::Udp ? "Wi-Fi (UDP)" : "ESP-NOW"; }
};
