#pragma once

// このファイルを include/secrets.h にコピーして値を書き換える。
// include/secrets.h は .gitignore 済みなのでコミットされない。
//
//   cp include/secrets.example.h include/secrets.h

// 接続するWi-Fi (ESP32は2.4GHz帯のみ対応)
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// テレメトリの送信先 (MacのIPアドレスとUDPポート)
#define TELEMETRY_HOST "192.168.1.10"
#define TELEMETRY_PORT 10015
