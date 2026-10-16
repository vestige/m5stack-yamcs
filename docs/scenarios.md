# M5Sat 運用シナリオ集

衛星 M5Sat（M5Stack）を、地上局（Mac の YAMCS）から運用するときの手順をまとめます。Milestone 5 の各ステップで実際に試したシナリオを、あとからやり直せる形にしています。

- 各ステップの仕様（パケットの形式やコマンドの一覧）は [README](../README.md) の Milestone 5 を見てください
- 「本物の衛星ではどうしているか」は [real-operations.md](real-operations.md) にまとめています
- YAMCS の各メニューの役割は [yamcs-web-ui.md](yamcs-web-ui.md) にまとめています

時刻について：YAMCS の Web UI や受信ログの時刻は **UTC** です。日本時間から9時間引いた時刻になります。

## 目次

| シナリオ | 内容 | 状態 |
| --- | --- | --- |
| [準備](#prepare) | 地上局を起動し、衛星とつながっていることを確かめる | |
| [回線の切り替え](#link) | Wi-Fi（UDP）と ESP-NOW を切り替える。離れて電波の強さを見る | ✅ |
| [5-1 衛星の健康状態を見る](#s5-1) | HK テレメトリの確認 | ✅ |
| [5-2 コマンドを送る](#s5-2) | Command Verification、BEEP、ペイロード電源と LED | ✅ |
| [5-3 イベントを追う](#s5-3) | 衛星からのイベント通知 | ✅ |
| [5-4 異常からの自動退避と復旧](#s5-4) | FDIR、アラームの確認、復旧の手順 | ✅ |
| [5-5 コマンドの安全装置](#s5-5) | 範囲の検査、送信の前提条件、承認フロー | ✅ |
| [5-6 時刻指定の観測](#s5-6) | 観測の予約・取り消し・中断 | ✅ |
| [5-7 手順書（コマンドスタック）で運用する](#s5-7) | 通信の確認、観測開始・終了、SAFEからの復旧 | ✅ |
| [5-8 〜 5-10](#planned) | 運用画面、パス運用、異常対応の訓練 | 予定 |

---

<a id="prepare"></a>

## 準備

| 手順 | 操作 | 期待する結果 |
| --- | --- | --- |
| 1 | `scripts/start.sh` で YAMCS と受信スクリプトを起動する | `yamcs: ... ready` と出る |
| 2 | ESP-NOW を使うときは、地上局の無線機（M5StickC）を Mac に USB でつなぐ | M5StickC の画面が緑の `AOS` になる |
| 3 | http://localhost:8090 を開き、インスタンス `gnss` を選ぶ | |
| 4 | **Links** を開く | `m5stack-gnss` の受信数が1秒ごとに増えている |
| 5 | **Events** を開き、▶（Start streaming）でライブ表示にする | 衛星を再起動すると `BOOT` が届く |

コマンドは Web UI の **Commanding → Send a command** から送ります。ターミナルからは次のように送れます。

```sh
python3 tools/send_command.py NO_OP
python3 tools/send_command.py BEEP Duration=300
python3 tools/send_command.py SET_MODE Mode=NOMINAL --approve   # 試験用: 承認待ちの待ち行列で自動で承認する
```

`scripts/status.sh` で、地上局が動いているかと YAMCS の受信数を確かめられます。終わるときは `scripts/stop.sh` です。

---

<a id="link"></a>

## 回線の切り替え

**目的**：場所の Wi-Fi 事情に合わせて回線を選ぶ。回線を切り替えると、地上が衛星と話せなくなることがあると体験する。

| 手順 | 操作 | 見るところ | 期待する結果 |
| --- | --- | --- | --- |
| 1 | M5Stack の右ボタン（C）を**2秒長押し**する | M5Stack の画面 | 「Switching link to ...」と出て再起動する |
| 2 | | Events | `BOOT`（理由: SOFTWARE）が届く（切り替え先の回線で地上とつながっていれば） |
| 3 | | M5Stack の INFO 画面の一番下の行 | 今の回線（`UDP ...` または `ESP-NOW ch1 ...`）が出る |

- Wi-Fi のアクセスポイントがつないだ機器どうしの通信を禁止している場所（クライアント分離）では、Wi-Fi に切り替えると地上に届かなくなる。M5StickC の画面は赤の `LOS` になる。もう一度長押しして ESP-NOW に戻す
- M5StickC の USB を抜き差ししても、受信スクリプトが自動でつなぎ直す

**離れて電波の強さを見る（Wi-Fi のとき）**

| 手順 | 操作 | 見るところ | 期待する結果 |
| --- | --- | --- | --- |
| 1 | `/M5Sat/Bus/WifiRssi` を開き、Chart にする | Telemetry → Parameters | 近くでは -40〜-50 dBm |
| 2 | M5Stack を持って別の部屋へ移動し、戻ってくる | 同じグラフ | 離れると下がり（-70 dBm 前後）、戻ると元に戻る |

金属の鍋や缶に入れてふたをすると、その場で大きく下がり、通信が途切れる。

---

<a id="s5-1"></a>

## 5-1 衛星の健康状態を見る

**目的**：HK（Housekeeping）テレメトリで、衛星の健康状態を確かめる。

| 手順 | 操作 | 見るところ | 期待する結果 |
| --- | --- | --- | --- |
| 1 | | Telemetry → Parameters の `/M5Sat/Bus/` | モード・ペイロード電源・コマンドカウンタ・空きメモリ・起動からの時間が1秒ごとに更新される |
| 2 | M5Stack のリセットボタンを押す | `/M5Sat/Bus/BootCount`、`ResetReason` | 再起動の回数が1つ増える。理由は `POWER_ON`（ESP32 の仕様で、リセットボタンもこうなる） |
| 3 | Wi-Fi のとき、アクセスポイントから離れる | `/M5Sat/Bus/WifiRssi` | RSSI が下がる（[回線の切り替え](#link) を参照） |

---

<a id="s5-2"></a>

## 5-2 コマンドを送る

**目的**：コマンドを送り、衛星が受理・実行したことを Command Verification で見届ける。

| 手順 | 操作 | 見るところ | 期待する結果 |
| --- | --- | --- | --- |
| 1 | `NO_OP` を送る | Commanding → Command history | Accepted → Complete と進む |
| 2 | `BEEP` を `Duration=300` で送る | M5Stack、Command history | M5Stack が「ピッ」と鳴る。鳴り終わってから Complete になる |
| 3 | `SET_TM_INTERVAL` を `Interval=2000` で送る | 受信ログ（`tail -f logs/udp_receiver.log`） | HK の届く間隔が2秒になる |
| 4 | `SET_TM_INTERVAL` を `Interval=1000` で送る | | 1秒に戻る |
| 5 | `PAYLOAD_POWER` を `State=OFF` で送る | M5Stack の LED と画面、受信ログ | LED が消え、画面のバッジが `PAYLOAD OFF` になる。GNSS テレメトリ（APID 100）が届かなくなる |
| 6 | `PAYLOAD_POWER` を `State=ON` で送る | | LED が点き、GNSS テレメトリが戻る |

- 範囲外の値（例：`Interval=50`）は、5-5 からは YAMCS が送る前に止める。衛星側でも同じ範囲で検査していて、衛星に届けば `INVALID_ARGUMENT` で拒否される
- 拒否されたコマンドの理由は `/M5Sat/Bus/AckErrorCode` と Events で見られる

---

<a id="s5-3"></a>

## 5-3 イベントを追う

**目的**：衛星から届くイベントで、何が起きたかを時系列で追う。

**準備**：Events をライブ表示にしたまま、別のタブで Commanding → Send a command を開く。

| 手順 | 操作 | Events に届くイベント |
| --- | --- | --- |
| 1 | M5Stack のリセットボタンを押す | INFO「起動しました (○回目, 理由: POWER_ON)」 |
| 2 | `SET_MODE` を `Mode=SAFE` で送る（Queues で承認する） | INFO「運用モードを NOMINAL から SAFE に変更しました」と「ペイロードの電源を OFF にしました」の2つ |
| 3 | `SET_MODE` を `Mode=NOMINAL` で送る（Queues で承認する） | INFO「運用モードを SAFE から NOMINAL に変更しました」 |
| 4 | `PAYLOAD_POWER` を `State=ON` で送る | INFO「ペイロードの電源を ON にしました」 |

---

<a id="s5-4"></a>

## 5-4 異常からの自動退避と復旧

**目的**：衛星が異常を自分で検知して SAFE へ退避し、地上が気づいて復旧させる、という一連の流れを体験する。

**準備**：Events（ライブ表示）と Alarms を開いておく。モードは NOMINAL、ペイロードは ON。

| 手順 | 操作 | 見るところ | 期待する結果 |
| --- | --- | --- | --- |
| 1 | GPS ユニットの Grove ケーブルを抜く | M5Stack、Events、Alarms | 10秒後に DISTRESS「異常を検知しました: GNSSのデータが届きません」→ ペイロード OFF（LED が消える）→ SAFE。Alarms に `Mode` の Warning が上がる |
| 2 | Alarms でアラームを確認する（Acknowledge を押す） | Alarms | 未確認（unacked）の数が減る |
| 3 | ケーブルを戻す | | |
| 4 | **復旧の手順**を行う（下を参照）。手順書（スタック）`04-safe-recovery.ycs` を使ってもよい（[5-7](#s5-7)） | | NOMINAL に戻り、GNSS のデータが届く。アラームは消える |

**復旧の手順**（5-5 の安全装置で、SAFE のままではペイロードを入れられない）

1. Events の `FAULT_DETECTED` で、何が起きたかを確かめる
2. 原因を直す（今回は GPS ユニットのケーブルを戻す）
3. `SET_MODE` を `Mode=NOMINAL` で送り、Commanding → Queues で承認する。`/M5Sat/Bus/Mode` が NOMINAL になったことを確かめる
4. `PAYLOAD_POWER` を `State=ON` で送り、`/M5Sat/GNSS/...` のデータが届き始めたことを確かめる

YAMCS は届いている最新のテレメトリで送信の前提条件を判断する。手順3の結果が HK に出る（最大1秒）のを待ってから手順4を送る。

**あわせて試すこと**

| 試すこと | 期待する結果 |
| --- | --- |
| ケーブルを抜いたまま復旧の手順を行う | ペイロードを入れてから30秒（最初のデータを待つ猶予）で、また SAFE へ退避する |
| `SET_FDIR` を `State=OFF` にしてから（Queues で承認）ケーブルを抜く | 退避しない。画面は `NO DATA` になる |
| 終わったら `SET_FDIR` を `State=ON` に戻す | `FDIR_CHANGED`（有効）が届く。ケーブルが抜けたままなら、また退避する |

**アラームの操作**

| 操作 | 意味 |
| --- | --- |
| Acknowledge | 「見ました、対応します」。正常に戻っていれば一覧から消える |
| Shelve | 「わかっているので、しばらく通知しないで」。期限まで一覧から隠れる |
| Unshelve | Shelve を取り消し、また通知されるようにする |

---

<a id="s5-5"></a>

## 5-5 コマンドの安全装置

**目的**：危ないコマンドが、地上で止められることを確かめる。

| 手順 | 操作 | 見るところ | 期待する結果 |
| --- | --- | --- | --- |
| 1 | `SET_TM_INTERVAL` を `Interval=50` で送ろうとする | Send a command の画面 | 範囲外（200〜10000）として、送る前に止められる |
| 2 | `SET_MODE` を `Mode=SAFE` で送る | Commanding → Queues | `supervised` の待ち行列に入る（待ち行列の表示は `HOLD`） |
| 3 | 待ち行列の中のコマンドを承認（Accept）する | M5Stack、Events | 衛星へ送られ、SAFE になる |
| 4 | SAFE のまま `PAYLOAD_POWER` を `State=ON` で送る | Command history | TransmissionConstraints が NOK（「Transmission constraints check failed」）。衛星には送られない |
| 5 | ペイロードが OFF のまま `SET_MODE` を `Mode=MISSION` で送り、承認する | Command history | 同じく、送信の前提条件で止められる |
| 6 | [5-4 の復旧の手順](#s5-4) で NOMINAL・ペイロード ON に戻す | | |

待ち行列そのものを `ACCEPT` に切り替えると、それ以降の critical のコマンドが承認なしで送られるようになる。待ち行列の状態は変えず、中のコマンドを1つずつ承認する。

---

<a id="s5-6"></a>

## 5-6 時刻指定の観測

**目的**：観測を予約し、衛星が予約した時刻に自分で観測を始めて終えることを確かめる。

**準備**：Events をライブ表示にしておく。モードは NOMINAL、ペイロードは ON。`/M5Sat/Bus/OnboardTime`（機上時刻）が今の UTC になっていることを確かめる（まだ GPS の時刻を受け取っていないと、予約は拒否される）。

| 手順 | 操作 | 見るところ | 期待する結果 |
| --- | --- | --- | --- |
| 1 | `SCHEDULE_OBSERVATION` を、`Start` に5分後の UTC 時刻、`Duration=180` で送り、Queues で承認する | Events、`/M5Sat/Bus/ObservationState` | INFO「観測を予約しました (○秒後から 180秒間)」。状態が `SCHEDULED` になる |
| 2 | 予約した時刻まで待つ | M5Stack のヘッダ、Events | 水色の `MISSION` になる。「観測を始めました」 |
| 3 | 観測中のデータを見る | `/M5Sat/GNSS/AverageCn0`、`TrackedCount` などを Chart に。`ObservedSatellites` を開く | まとめの値が1秒ごとに更新される。衛星ごとの一覧（最大32機）が見られる |
| 4 | 3分待つ | M5Stack、Events | NOMINAL に戻り、「観測を終えました」 |

**あわせて試すこと**

| 試すこと | 期待する結果 |
| --- | --- |
| 予約してから `CANCEL_OBSERVATION` を送る（Queues で承認） | 「観測の予約を取り消しました」。予約した時刻を過ぎても観測は始まらない |
| 観測が始まったら `PAYLOAD_POWER` を `State=OFF` で送る | NOMINAL に戻り、WARNING「観測を中断しました」 |
| 予約したあと、開始時刻の前にペイロードを OFF にする | 開始時刻に WARNING「予約した観測を見送りました: ペイロードの電源がOFFです」 |

---

<a id="s5-7"></a>

## 5-7 手順書（コマンドスタック）で運用する

**目的**：決まった作業を、手順書（スタック）に沿って1行ずつ確かめながら進める。

**準備**：**Procedures → Stacks** を開く。4つのスタックが並んでいる。承認のために、別のタブで **Commanding → Queues** も開いておく。

| スタック | 使う場面 |
| --- | --- |
| `01-link-check.ycs` | パスの始めに、テレメトリとコマンドがやり取りできることを確かめる |
| `02-observation-start.ycs` | 観測を手で始める |
| `03-observation-end.ycs` | 観測を手で終える |
| `04-safe-recovery.ycs` | FDIR などで SAFE へ退避したあと、元に戻す |

**スタックの進め方**

| ステップ | 何が起きるか | 運用者がすること |
| --- | --- | --- |
| `text` | 説明が表示される | 読む |
| `check` | パラメータの今の値が表示される | 目で確かめて、次へ進める |
| `command` | コマンドが送られる。Command Verification が完了するまで次へ進まない | critical のコマンド（`SET_MODE`）は、Queues で承認する |
| `verify` | テレメトリが条件を満たすまで待つ | 待つ。時間切れで止まったら、原因を調べる |

実行のしかたは、1ステップずつ進める方法と、続けて実行する方法がある。初めは1ステップずつ進め、慣れたら続けて実行する（承認待ちと時間切れのところだけで止まる）。

| 手順 | 操作 | 期待する結果 |
| --- | --- | --- |
| 1 | `01-link-check.ycs` を実行する | `NO_OP` が完了し、受理したコマンド数が増える |
| 2 | `02-observation-start.ycs` を実行する。`SET_MODE` で止まったら Queues で承認する | MISSION になり、観測データが届き始める |
| 3 | `03-observation-end.ycs` を実行する（同じく承認する） | NOMINAL に戻り、ペイロードが OFF になる |
| 4 | GPS ユニットのケーブルを抜き、FDIR で SAFE へ退避させる（[5-4](#s5-4)） | SAFE になり、アラームが上がる |
| 5 | アラームを確認し、ケーブルを戻してから `04-safe-recovery.ycs` を実行する | NOMINAL に戻り、GNSS のデータが届く。35秒後も NOMINAL のまま |

**あわせて試すこと**

| 試すこと | 期待する結果 |
| --- | --- |
| ケーブルを抜いたまま `04-safe-recovery.ycs` を実行する | 「GNSSのデータが届き始めた」の `verify` が時間切れになり、スタックが止まる |
| `02-observation-start.ycs` の `SET_MODE` を、Queues で承認せずに放っておく | 承認するまで、スタックは先へ進まない |

---

<a id="planned"></a>

## 5-8 〜 5-10（予定）

これから作るステップのシナリオです。作りながら育てていきます。

### 5-8 運用画面

運用者は運用画面だけを見ながら、5-7 の「観測開始」から「観測終了」までの手順を進める。他のメニューを開かなくても、各手順の結果を確認できることを確かめる。

### 5-9 Timeline とパス運用

運用者は10分ごとに2分間のパスと、パスの間の観測を Timeline に計画する。パスが始まったら HK を確認し、次の観測を予約してパスを終える。後で Archive browser を開き、観測が計画どおりの時間に行われたかを確認する。

### 5-10 異常対応の訓練

訓練の出題者は、運用者に知らせずに異常を1つ起こす。運用者はアラームやイベントから異常に気づき、Command history や HK を手がかりに原因を推定し、手順書（`04-safe-recovery.ycs`）で衛星を元に戻す。気づくまでと復旧までにかかった時間を記録する。
