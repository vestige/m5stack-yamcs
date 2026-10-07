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

### Milestone 3 - UDP Telemetry ✅

M5StackからWi-Fi経由でMacへTelemetryをUDP送信します。

Milestone 4でYAMCSへそのまま取り込めるよう、パケットは実際の衛星でも使われる **CCSDS Space Packet** の形式にしています。1秒に1回、48バイトのパケットを送ります。

#### セットアップ

1. Wi-Fiと送信先の設定ファイルを作ります。`include/secrets.h` は `.gitignore` に入っているので、コミットされません。

   ```sh
   cp include/secrets.example.h include/secrets.h
   ```

2. `include/secrets.h` にWi-FiのSSIDとパスワード、送信先のMacのIPアドレスを書きます。MacのIPアドレスは `ipconfig getifaddr en0` で確認できます。ESP32は2.4GHz帯のWi-Fiにしか接続できません。

3. Macで受信スクリプトを起動します（Python標準ライブラリのみで動きます）。初回はmacOSのファイアウォールが受信を許可するか尋ねてきます。

   ```sh
   python3 tools/udp_receiver.py
   ```

   ```text
   [192.168.1.23] apid=100 seq=   42 2026-10-05T02:13:45.000Z lat=33.590355 lon=130.401716 alt=12.3m sats=12 fix=GPS(1) type=3D rmc=A uptime=43.0s ok=1520 err=0
   ```

   YAMCSと一緒に動かす場合は、Milestone 4の `scripts/start.sh` を使ってください。受信スクリプトも一緒に起動されます。

M5StackのINFO画面の下部にWi-Fiの接続状態と送信したパケット数（`tx=`）が表示されます。

#### パケット形式

数値はすべてビッグエンディアンです。詳しくは [lib/TelemetryPacket](lib/TelemetryPacket/src/TelemetryPacket.h) を参照してください。

| Offset | Size | 型 | 内容 |
| --- | --- | --- | --- |
| 0 | 6 | - | CCSDS Primary Header（TM、APID=100、Sequence count） |
| 6 | 4 | uint32 | UTC時刻（Unix秒。無効なら0） |
| 10 | 2 | uint16 | UTCミリ秒 |
| 12 | 8 | float64 | 緯度（度） |
| 20 | 8 | float64 | 経度（度） |
| 28 | 4 | float32 | 海抜高度（m） |
| 32 | 1 | uint8 | 測位に使用している衛星数 |
| 33 | 1 | uint8 | Fix quality（GGA） |
| 34 | 1 | uint8 | Fix type（1=No fix, 2=2D, 3=3D） |
| 35 | 1 | uint8 | Flags（bit0:時刻有効, bit1:日付有効, bit2:位置有効, bit3:高度有効, bit4:RMC Active） |
| 36 | 4 | uint32 | 起動からの経過時間（ms） |
| 40 | 4 | uint32 | 正常に受信したNMEAセンテンス数 |
| 44 | 4 | uint32 | チェックサムエラー数 |

Sequence countは送信のたびに1増えます（14bitで一周）。受信スクリプトはこの番号の飛びからパケットロスを検出します。

### Milestone 4 - YAMCS ✅

UDPで受信したTelemetryをYAMCSへ取り込み、Mission Databaseでパラメータとして定義します。

```text
M5Stack ──UDP:10015──▶ tools/udp_receiver.py ──UDP:10016──▶ YAMCS ──▶ Web UI (http://localhost:8090)
                        (受信ログ logs/udp_receiver.log)
```

YAMCSとMilestone 3の受信スクリプトは同じポートを同時に使えません。そのため受信スクリプトが10015番で受けてログを取り、同じパケットをYAMCS（10016番）へ中継します。M5Stack側の設定は変えなくて構いません。

#### セットアップ

YAMCSはJavaで動きます。初回だけJava 17をインストールしてください。

```sh
brew install openjdk@17
```

#### 起動と終了

```sh
scripts/start.sh    # YAMCS と受信スクリプトをバックグラウンドで起動
scripts/status.sh   # 動いているかと、YAMCSが受信したパケット数を表示
scripts/stop.sh     # 両方を終了
```

初回の起動は、YAMCSの依存ライブラリをダウンロードするため数分かかります。ログは `logs/` に出力されます。

起動したら http://localhost:8090 を開き、インスタンス `gnss` を選びます。Telemetry → Parameters で `/M5Sat/GNSS/Latitude` などの値を見られます。パラメータを開くとグラフも表示されます。

#### YAMCSの構成

| ファイル | 内容 |
| --- | --- |
| [yamcs/src/main/yamcs/mdb/m5sat.xml](yamcs/src/main/yamcs/mdb/m5sat.xml) | Mission Database（XTCE）。パケットのどこに何が入っているかを定義する。`/M5Sat/Bus`（バス）と `/M5Sat/GNSS`（ペイロード）に分かれる |
| [yamcs/src/main/yamcs/etc/yamcs.gnss.yaml](yamcs/src/main/yamcs/etc/yamcs.gnss.yaml) | インスタンス `gnss` の設定。TMのデータリンク（10016番で受信）、TCのデータリンク（10025番へ送信）、パケットの前処理と後処理 |
| [yamcs/pom.xml](yamcs/pom.xml) | YAMCSのバージョン（公式 [quickstart](https://github.com/yamcs/quickstart) がベース） |

パラメータは Milestone 3 のパケット形式と1対1で対応しています（`/M5Sat/GNSS/UtcTime`、`/M5Sat/GNSS/Latitude`、`/M5Sat/GNSS/Satellites` など）。次のアラームを定義しています。

| パラメータ | 条件 | レベル |
| --- | --- | --- |
| `/M5Sat/GNSS/Satellites` | 4機未満（3D測位できない） | Warning |
| `/M5Sat/GNSS/FixType` | `NoFix` | Warning |

受信時刻にはYAMCSが受け取った時刻を使います。GNSSから得たUTC時刻は、パラメータ `/M5Sat/GNSS/UtcTime` として見られます。保存したテレメトリは `yamcs/target/yamcs/yamcs-data` に入ります。`mvn clean` で消えます。

### Milestone 5 - Satellite Operations Simulation

M5Stackを1機の衛星に見立てて、YAMCSを地上局として運用する模擬実験をします。テレメトリを受けるだけでなく、地上からコマンドを送り、衛星が実行したことを確かめるところまでを体験します。

```text
            ┌─────────── 衛星 (M5Stack) ───────────┐
            │  バス:      モード管理・HK・コマンド処理  │
            │  ペイロード: GNSS受信機 (観測機器)         │
            └──────────────────────────────────────┘
               ▲ TC (UDP)                  │ TM (UDP)
               │                           ▼
            ┌─────────── 地上局 (Mac) ──────────────┐
            │  YAMCS: コマンド送信・実行確認・Timeline  │
            └──────────────────────────────────────┘
```

衛星は、バス（衛星を動かすための共通部分）とペイロード（ミッションの観測機器）に分けて考えます。このプロジェクトでは、GNSS受信機をペイロードとして扱います。YAMCSのメニューごとの役割は [docs/yamcs-web-ui.md](docs/yamcs-web-ui.md) にまとめています。

#### 想定ミッション

> **M5Sat** は、地上に届くGNSS衛星の電波の強さを観測する衛星です。運用チームは、計画した時間帯にペイロードで観測を行い、観測データを地上で回収します。衛星に異常があれば、衛星は自分で安全なモードへ退避し、運用チームが原因を調べて復旧させます。

各ステップの最後に、このミッションに沿った**運用シナリオ**を置きます。シナリオをYAMCS上で実際に操作して最後まで進められたら、そのステップは完了です。シナリオは作りながら育てていきます。

#### 進め方

1ステップを1つのPRにします。後のステップは前のステップの機能を使うので、上から順に進めます。

| Step | 内容 | 主に使うYAMCSの機能 |
| --- | --- | --- |
| 5-1 | Housekeeping テレメトリ | Telemetry |
| 5-2 | Telecommand と Command Verification | Commanding |
| 5-3 | 衛星からのイベント通知 | Events |
| 5-4 | 運用モードと異常時の自動退避（FDIR） | Alarms, Events |
| 5-5 | コマンドの安全装置 | Mission database, Commanding（Queues） |
| 5-6 | ペイロード運用と観測データ | Telemetry, Commanding |
| 5-7 | 運用手順（コマンドスタック） | Procedures（Stacks） |
| 5-8 | 運用画面 | Parameter lists, Displays |
| 5-9 | Timeline とパス運用 | Timeline, Links, Archive browser |
| 5-10 | 異常対応の訓練 | Alarms, Events, Command history |

#### 5-1 Housekeeping テレメトリ ✅

衛星の健康状態を表すHK（Housekeeping）テレメトリを、GNSSテレメトリとは別のAPID（101）で、1秒ごとに送ります。YAMCSでは `/M5Sat/Bus/` の下にパラメータとして並びます。

| Offset | Size | 型 | パラメータ | 内容 |
| --- | --- | --- | --- | --- |
| 0 | 6 | - | - | CCSDS Primary Header（TM、APID=101、Sequence countはAPIDごと） |
| 6 | 1 | uint8 | `Mode` | 運用モード（0=SAFE, 1=NOMINAL, 2=MISSION） |
| 7 | 1 | uint8 | `PayloadPower` | ペイロードの電源（0=OFF, 1=ON） |
| 8 | 2 | uint16 | `AcceptedCommands` | 受理したコマンド数 |
| 10 | 2 | uint16 | `RejectedCommands` | 拒否したコマンド数 |
| 12 | 1 | uint8 | `LastCommandId` | 最後に実行したコマンドのID（0=なし） |
| 13 | 1 | int8 | `WifiRssi` | Wi-Fiの電波の強さ（dBm） |
| 14 | 4 | uint32 | `FreeHeap` | ヒープの空き容量（bytes） |
| 18 | 4 | uint32 | `MinFreeHeap` | 起動してからのヒープの空き容量の最小値（bytes） |
| 22 | 4 | uint32 | `Uptime` | 起動からの経過時間（ms） |
| 26 | 2 | uint16 | `BootCount` | 再起動の回数。電源を切っても消えないよう、M5Stackのフラッシュ（NVS）に保存する |
| 28 | 1 | uint8 | `ResetReason` | 前回の再起動の理由（ESP-IDF の `esp_reset_reason_t`） |

詳しくは [lib/TelemetryPacket/src/HousekeepingPacket.h](lib/TelemetryPacket/src/HousekeepingPacket.h) を参照してください。

- 運用モードとペイロードの電源は、切り替えを実装する 5-2 / 5-4 までは NOMINAL・ON 固定です。コマンドカウンタも 5-2 までは 0 のままです。
- リセットボタンによる再起動は、ESP32の仕様で `POWER_ON` として記録されます。
- 電池残量はGrove Port Aと内部I2Cのピンが共用のため読めません（Milestone 2参照）。

**運用シナリオ**：運用者はTelemetry → Parametersで衛星の健康状態を確認する。M5Stackを再起動すると再起動の回数が増え、Wi-Fiのアクセスポイントから離すとRSSIが下がることを確かめる。

#### 5-2 Telecommand と Command Verification ✅

YAMCSからM5StackへCCSDSのTCパケットをUDPで送ります。コマンドはMission Databaseに定義し、Web UIの Commanding → Send a command から送信します。

```text
YAMCS ──TC──▶ :10025 tools/udp_receiver.py ──TC──▶ M5Stack :10025
  ▲                  (地上局の中継)                        │
  └──── :10016 ◀── :10015 ◀────────── TM (HK / ACK / GNSS) ┘
```

受信スクリプトは地上局として、TCの中継も受け持ちます。TCは、直近にTMを送ってきたM5StackのIPアドレスへ送ります。TMが届いていない（衛星が見えていない）間は、TCは送れません。本物の地上局と同じです。

| ID | コマンド | 引数 | 内容 |
| --- | --- | --- | --- |
| 1 | `NO_OP` | なし | 何もしない。通信路の確認用 |
| 2 | `SET_MODE` | `Mode`（SAFE / NOMINAL / MISSION） | 運用モードを切り替える。SAFEにするとペイロードもOFFになる。ペイロードがOFFのときはMISSIONにできない |
| 3 | `SET_TM_INTERVAL` | `Interval`（ms） | テレメトリの送信周期を変える。200〜10000ms 以外は拒否する |
| 4 | `PAYLOAD_POWER` | `State`（ON / OFF） | ペイロードの電源をON / OFFする。OFFの間はGNSSテレメトリを送らない。MISSION中にOFFにするとNOMINALに戻る |
| 5 | `RESET_COUNTERS` | なし | コマンドカウンタをリセットする |
| 6 | `BEEP` | `Duration`（ms） | スピーカーを鳴らす。50〜2000ms 以外は拒否する。鳴り終わってから実行完了になる |

範囲のチェックは、今は**衛星側だけ**で行っています。範囲外の値も地上からは送れてしまい、衛星が拒否します。地上側で止める仕組みは 5-5 で入れます。

TCパケット（APID 110）は、CCSDS Primary Header の後ろにコマンドID（1バイト）と引数（ビッグエンディアン）が続きます。長さフィールドとシーケンス番号は、YAMCSの `IssCommandPostprocessor` が埋めます。

##### コマンドの応答（ACK、APID 102）

衛星はTCを受けると、ACKパケットを返します。

| Offset | Size | 型 | パラメータ | 内容 |
| --- | --- | --- | --- | --- |
| 0 | 6 | - | - | CCSDS Primary Header（TM、APID=102） |
| 6 | 2 | uint16 | `AckTcSequence` | どのTCへの応答か（TCのシーケンス番号） |
| 8 | 1 | uint8 | `AckCommandId` | コマンド |
| 9 | 1 | uint8 | `AckStage` | 1=ACCEPTED（受理）, 2=REJECTED（拒否）, 3=COMPLETED（実行完了）, 4=FAILED（実行失敗） |
| 10 | 1 | uint8 | `AckErrorCode` | 0=なし, 1=知らないコマンド, 2=引数の長さが違う, 3=引数が範囲外, 4=今の状態では実行できない, 5=TCパケットとして不正 |

YAMCSのMission Databaseでは、ACKのシーケンス番号と、コマンド履歴に残ったTCのシーケンス番号（`/yamcs/cmdHist/ccsds-seqcount`）を照らし合わせるVerifierを定義しています。

| Verifier | 条件 | 待つ時間 |
| --- | --- | --- |
| Accepted | ACKの段階が ACCEPTED | 5秒 |
| Complete | ACKの段階が COMPLETED | 10秒 |
| Failed | ACKの段階が REJECTED か FAILED | 10秒 |

Commanding → Command history を開くと、コマンドごとに Accepted → Complete と進む様子、または Failed になった様子を確認できます。拒否の理由は `/M5Sat/Bus/AckErrorCode` で見られます。

**運用シナリオ**：運用者は `NO_OP` を送って通信路を確認し、Command historyで受理から実行完了までを見届ける。続けて `BEEP` を送ってM5Stackが鳴ることを確かめ、`SET_TM_INTERVAL` でテレメトリの周期を変える。範囲外の周期を送ると拒否され、HKの拒否カウンタが増えることを確認する。`PAYLOAD_POWER OFF` でGNSSテレメトリが止まり、そのまま `SET_MODE MISSION` を送ると拒否されることも確かめる。

#### 5-3 衛星からのイベント通知

衛星が「モードが変わった」「コマンドを拒否した」などの出来事を、イベント用のパケットで地上に知らせます。YAMCSではEventsに文章で並ぶので、何が起きたかを時系列で追えます。

**運用シナリオ**：運用者はEventsを開いたまま、5-2のコマンドを順に送る。コマンドごとに衛星からイベントが届き、拒否されたコマンドは理由つきでWarningとして表示されることを確かめる。

#### 5-4 運用モードと異常時の自動退避（FDIR）

衛星側で運用モードを管理します。

| モード | 状態 |
| --- | --- |
| SAFE | 最低限の機能だけ動かす。ペイロードはOFF |
| NOMINAL | 通常運用。HKとGNSSテレメトリを送る |
| MISSION | ペイロードで観測し、観測データを送る |

異常を検知したら、衛星が自分でSAFEモードへ移る仕組み（FDIR: Fault Detection, Isolation and Recovery）を入れます。たとえば、GNSSのデータが一定時間届かなければペイロード異常とみなします。

**運用シナリオ**：NOMINALで運用中にGPSユニットのケーブルを抜く。衛星は自分でSAFEへ移り、地上ではアラームとイベントが上がる。運用者はAlarmsでアラームを確認（Acknowledge）し、ケーブルを戻してから `SET_MODE NOMINAL` で復旧させる。

#### 5-5 コマンドの安全装置

誤ったコマンドで衛星を危険にさらさないための仕組みを、地上側に入れます。

- **送信の前提条件（Transmission Constraint）**：MDBに「このコマンドを送ってよい条件」を定義する。たとえば SAFEモード中は `PAYLOAD_POWER ON` を送れなくする
- **重要度（Significance）**：衛星の状態を大きく変えるコマンドに重要度を付け、送信前に確認を求める
- **承認フロー（Queues）**：重要なコマンドを待ち行列に溜め、運用者が承認してから送る

**運用シナリオ**：SAFEモードのまま `PAYLOAD_POWER ON` を送ろうとすると、地上で止められることを確かめる。`SET_MODE MISSION` は待ち行列に入り、Commanding → Queuesで承認してはじめて衛星へ送られることを確かめる。

#### 5-6 ペイロード運用と観測データ

ペイロードの電源がONで、MISSIONモードのときだけ観測データを送ります。観測データは、SATS画面で見ている衛星ごとの信号強度（C/N0）・仰角・方位角の一覧で、さらに別のAPIDで送ります。

観測は時刻指定コマンドで予約できるようにします。「UTC 12:00から10分間観測する」のように予約しておくと、衛星がGNSSの時刻を見て自分で観測を始め、自分で終えます。本物の衛星と同じく、地上と通信できない間も計画どおりに動くことを確かめます。

**運用シナリオ**：運用者は5分後から3分間の観測を予約する。予約した時刻になると衛星がMISSIONへ移って観測データを送り始め、3分後にNOMINALへ戻る。観測中の衛星ごとのC/N0をグラフで確認する。

#### 5-7 運用手順（コマンドスタック）

決まった作業の手順を、複数のコマンドを順番に並べたコマンドスタックとして用意します。本物の運用での手順書の役割です。

| 手順 | 内容 |
| --- | --- |
| 観測開始 | `PAYLOAD_POWER ON` → ペイロードの状態を確認 → `SET_MODE MISSION` |
| 観測終了 | `SET_MODE NOMINAL` → `PAYLOAD_POWER OFF` |
| SAFEからの復旧 | 状態を確認 → `RESET_COUNTERS` → `SET_MODE NOMINAL` |

**運用シナリオ**：運用者はProcedures → Stacksで「観測開始」のスタックを1行ずつ実行し、各コマンドの実行完了を確認してから次へ進む。途中のコマンドが拒否されたら、そこで止まることを確かめる。

#### 5-8 運用画面

運用者が1画面で衛星の状態を把握できるよう、モード・ペイロードの状態・アラーム・コマンドカウンタ・GNSSの状態をまとめた画面を、Parameter listsとDisplaysで作ります。

**運用シナリオ**：運用者は運用画面だけを見ながら、5-7の「観測開始」から「観測終了」までの手順を進める。他のメニューを開かなくても、各手順の結果を確認できることを確かめる。

#### 5-9 Timeline とパス運用

YAMCSのTimelineに運用計画を書き込みます。

- 通信できる時間帯（パス）
- 観測の予定（5-6の時刻指定コマンドと対応させる）
- 運用モードを切り替える予定

パスの時間だけデータリンクを有効にして、衛星と通信できる時間が限られている状況を再現します。パスが終わったら、Archive browserで残ったデータと計画を時間軸の上で照らし合わせて振り返ります。

**運用シナリオ**：運用者は10分ごとに2分間のパスと、パスの間の観測をTimelineに計画する。パスが始まったらHKを確認し、次の観測を予約してパスを終える。後でArchive browserを開き、観測が計画どおりの時間に行われたかを確認する。

#### 5-10 異常対応の訓練

異常を意図的に起こすデバッグ用のコマンドを用意して、異常対応を訓練します。

| 訓練用の異常 | 内容 |
| --- | --- |
| GNSS受信の停止 | GNSSのデータが届かなくなったことにする |
| コマンドの無視 | 衛星が一定時間コマンドに応答しなくなる |
| 再起動 | 衛星が予告なく再起動する |

**運用シナリオ**：訓練の出題者は、運用者に知らせずに異常を1つ起こす。運用者はアラームやイベントから異常に気づき、Command historyやHKを手がかりに原因を推定し、5-7の復旧手順で衛星を元に戻す。気づくまでと復旧までにかかった時間を記録する。

#### 発展

- **データレコーダと後送り（Store and Forward）**：Wi-Fiが切れている間のテレメトリを衛星側に記録しておき、次のパスで送る。YAMCSでは `tm_dump` ストリームで受ける
- **機上時刻の利用**：パケットの生成時刻を、YAMCSの受信時刻ではなく衛星のGNSS時刻にする
- **誤り検出**：TM / TCパケットにCRCを付け、壊れたパケットを捨てる
- **ファイル転送**：観測データをファイルとしてまとめ、File transfer（CFDP）で地上へ送る
- **運用の自動化**：Pythonの `yamcs-client` で、アラームの通知や日々の集計を自動化する
