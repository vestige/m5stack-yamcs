#pragma once

// このファイルを include/secrets.h にコピーして値を書き換える。
// include/secrets.h は .gitignore 済みなのでコミットされない。
//
//   cp include/secrets.example.h include/secrets.h

// 地上局との回線
//   0: Wi-Fiのアクセスポイントにつなぎ、Mac と UDP でやり取りする (下の WIFI_* と TELEMETRY_* を使う)
//   1: 地上局の無線機 (M5StickC, src/ground/) と ESP-NOW でやり取りする。Wi-Fiの設定は使わない
#define GROUND_LINK_ESPNOW 0
#define ESPNOW_CHANNEL 1

// 接続するWi-Fi (ESP32は2.4GHz帯のみ対応)
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// テレメトリの送信先 (MacのIPアドレスとUDPポート)
#define TELEMETRY_HOST "192.168.1.10"
#define TELEMETRY_PORT 10015
