# m5stack-yamcs

M5StackとGNSSユニットを使って、YAMCSによる衛星テレメトリの仕組みを学ぶための実験プロジェクトです。

実際のGNSS衛星から受信したデータをM5Stackで取得し、最終的にYAMCSへTelemetryとして送信・可視化することを目標にしています。

## Goal

最終的には以下の構成を目指します。

```text
GNSS Satellites
      ↓ RF
Unit GPS SMA
      ↓ UART
M5Stack Core ESP32
      ↓ Wi-Fi / UDP
Mac
      ↓
YAMCS
      ↓
Telemetry / Archive / Monitor
```

将来的にはYAMCSからM5StackへのTelecommand（TC）も試し、簡単なMission Control Systemを構築します。

## Hardware

- M5Stack Core ESP32
- Unit GPS SMA
- Mac

## Development Environment

- VS Code
- PlatformIO
- Arduino Framework

## Progress

### Milestone 1 - GNSS Reception ✅

Unit GPS SMAをM5StackのGroveポートへ接続し、UART経由でNMEAデータを受信できることを確認しました。

確認できたNMEAメッセージの例：

```text
$GNGGA
$GNRMC
$GNGSA
$GPGSV
$GLGSV
$GAGSV
$BDGSV
$GQGSV
```

GPS / GLONASS / Galileo / BeiDou / QZSSなど、複数のGNSS衛星群からの情報を確認しています。

### Milestone 2 - Telemetry Extraction ✅

NMEAデータから以下の情報を抽出します。

- Time
- Latitude
- Longitude
- Altitude
- Satellite count
- Fix status


外部ライブラリは使わず、NMEAパーサを [lib/NmeaParser](lib/NmeaParser/src/NmeaParser.h) に自作しています。チェックサムを検証したうえで、以下のセンテンスから値を取り出します（Talker ID は `GN` / `GP` などを問いません）。

| Sentence | 抽出する項目 |
| --- | --- |
| GGA | Time, Latitude, Longitude, Altitude, Satellite count, Fix quality |
| RMC | Date, Status (A/V) |
| GSA | Fix type (No fix / 2D / 3D) |
| GSV | 衛星ごとの番号・仰角・方位角・信号強度 (C/N0) |

M5Stackの画面は1秒ごとに更新し、ボタンでページを切り替えます。画面表示には [M5Unified](https://github.com/m5stack/M5Unified) を使っています。

| ボタン | 画面 | 内容 |
| --- | --- | --- |
| A（左） | INFO | 日付・時刻・緯度経度・高度・衛星数 |
| B（中） | SATS | 衛星システム別の内訳と、衛星ごとの信号強度 |
| C（右） | SYS | 衛星システムごとの名前・運用国と、見えている衛星番号の一覧 |

右上のバッジはFix statusを表し、`3D FIX`（緑）、`2D FIX`（黄）、`NO FIX`（赤）、`NO DATA`（灰：3秒以上NMEAを受信できていない）のいずれかになります。

SATS画面の見方：

- 上段のタイルは衛星システム別に「信号を捕捉している数 / 見えている数」を表示します（GPS・GLONASS・Galileo・BeiDou・QZSS）。
- 下段のバーは衛星ごとの信号強度（C/N0、dB-Hz）です。強い順に最大22機まで表示し、点線は良好な信号の目安（40 dB-Hz）です。
- 衛星番号の頭文字はRINEX形式の略号です（G=GPS、R=GLONASS、E=Galileo、C=BeiDou、J=QZSS）。

SYS画面では、GPS（アメリカ）・GLONASS（ロシア）・Galileo（欧州）・BeiDou（中国）・QZSS みちびき（日本）ごとに衛星番号を並べます。信号を捕捉している衛星はシステムの色で、見えているが捕捉していない衛星は灰色で表示します。

> M5Stack Core（Basic）のGrove Port A（GPIO21/22）は内部I2Cと同じピンです。そのため起動時に内部I2Cを解放し、そのピンをGPSのUARTに割り当てています。

同じ内容をシリアルへも出力します。

```text
[TLM] 2026-10-05T02:13:45.000Z lat=33.590355 lon=130.401716 alt=12.3m sats=12 fix=GPS(1) type=3D rmc=A ok=1520 err=0
```

受信したNMEAをそのまま見たい場合は [src/main.cpp](src/main.cpp) の `ECHO_RAW_NMEA` を `1` にします。

パーサはArduinoに依存しないため、Mac上でユニットテストを実行できます。

```sh
pio test -e native
```

### Milestone 3 - UDP Telemetry

M5StackからWi-Fi経由でMacへTelemetryをUDP送信します。

### Milestone 4 - YAMCS

UDPで受信したTelemetryをYAMCSへ取り込み、Mission Databaseでパラメータとして定義します。

### Future

YAMCSからM5StackへのTelecommand送信とCommand Verificationも試す予定です。
