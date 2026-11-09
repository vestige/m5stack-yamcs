# 冗長化の実験 - 死活監視と待機系への切替

YAMCS を2つ (Active 系と Standby 系) 動かし、死活監視・切替・二重送信の防止を手元の Mac で確かめるための実験環境です。

地上システムを2重にして、片方が止まってももう片方で運用を続ける（冗長化）とき、次のことを決めておく必要があります。

- **死活監視**：止まったことを、何で、何秒で見つけるか
- **切替**：Standby 系を Active 系にする手順と、かかる時間
- **二重送信の防止**：古い Active 系と新しい Active 系の両方から、衛星にコマンドが出てしまわないようにする

これを本物の YAMCS で試して、数字と、うまくいかないところを確かめるのが目的です。M5Stack がなくても動くように、衛星と地上局は模擬（`tools/fake_sat.py`）にしています。

> いつもの `scripts/start.sh`（ポート 8090）とは別のポートを使うので、同時に動かしてもぶつかりません。

## 構成

```text
                         ┌──────────── Active 系 ───────────────────────────────┐
 tools/fake_sat.py       │ YAMCS primary  http://localhost:8190                 │
 (模擬の衛星・地上局)  ──TM udp:10116──▶ sat-tm   sat-tc ──TC udp:10125──▶ fake_sat │
                         │      ReplicationMaster (tm_realtime, cmdhist_realtime)│
                         └───────────────────────┬─────────────────────────────┘
                                                 │ レプリケーション tcp:8099
                         ┌───────────────────────▼──── Standby 系 ─────────────┐
                         │ YAMCS standby  http://localhost:8191                 │
                         │      ReplicationSlave                                │
                         │      sat-tm (udp:10216) / sat-tc (udp:10126) は      │
                         │      起動時から無効。切替で有効にする               │
                         └─────────────────────────────────────────────────────┘

 tools/watchdog.py  … 両方の系の外から 1 秒ごとに API を見る (検知だけで、切替はしない)
 tools/failover.py  … 承認 → 旧 Active 系の隔離 → 隔離の確認 → レプリケーション停止 → 昇格
 chaos.sh           … 障害を起こす (強制終了・一時停止・再起動)
```

- 地上局の「接続先」は `.run/target`（`primary` / `standby`）で決まります。地上局がどちらの系とつながるかを切り替えることにあたります。
- TC は系ごとに別のポート（10125 / 10126）に届くようにしてあるので、地上局はどちらの系から届いたかがわかります。接続先でない系から届いたら、二重送信の疑いとして警告します。
- MDB（`m5sat.xml`）と `processor.yaml` は本体（`yamcs/src/main/yamcs/`）のものを `start.sh` がコピーして使います。

## 起動と終了

```sh
experiments/failover/start.sh          # primary → standby → fake_sat の順に起動 (初回は依存の取得で数分)
python3 experiments/failover/tools/watchdog.py    # 別のターミナルで。監視を画面に出し続ける

experiments/failover/stop.sh           # 止める
experiments/failover/stop.sh --clean   # 止めて、保存データ・ログ・切替の状態も消す (実験をやり直すとき)
```

実験は毎回 `stop.sh --clean` → `start.sh` から始めると、前の実験の状態（リンクの有効・無効など）が残りません。

## 実験

切替の手順（`tools/failover.py`）は、次の5つに分けています。

| 手順 | すること |
| --- | --- |
| 1 承認 | 両方の系の状態を出し、運用者の承認を待つ（検知は自動、切替は人が決める） |
| 2 隔離 | 旧 Active 系の TC・TM リンクを API で無効にする。API が答えなければ、プロセスを止める（2-a / 2-b / 2-c） |
| 3 隔離の確認 | 旧 Active 系の TC リンクが無効か、プロセスが止まっていることを確かめる。確かめられなければ中止する |
| 4 昇格 | Standby 系のレプリケーションを止め（4-0）、TM リンクを有効にし、地上局の接続先を切り替え、TC リンクを有効にする |
| 5 再開前の確認 | コマンド履歴など、運用を再開する前に見ることを出す |

| ID | 確かめること | 関わる手順 |
| --- | --- | --- |
| E1 | プロセスが落ちたとき、何秒で検知できるか | 死活監視 |
| E2 | ハングアップ（応答しないがポートは開いている）を検知できるか | 死活監視 |
| E3 | Standby 系が止まっていた間の TM を、再接続後に取り戻せるか | レプリケーション |
| E4 | 承認つきの切替にかかる時間と、切替後に TC が通るか。再起動なしで昇格できるか | 1〜4 |
| E5 | 旧 Active 系が応答しないとき、隔離がプロセスの停止に切り替わるか | 2・3 |
| E6 | 隔離を飛ばしたとき、二重送信を検知できるか | 二重送信の防止 |
| E7 | 隔離した系が自動で再起動しても、TC リンクは無効のままか | 2 |
| E8 | 旧 Active 系で送ったコマンドの履歴が、Standby 系に複製されているか | 5 |
| E9 | 切替後に旧 Active 系が復帰しても、そのデータが新 Active 系に流れ込まないか | 4-0 |

### E1 プロセスの停止を検知する

```sh
experiments/failover/chaos.sh kill primary
```

- watchdog に `ALERT primary 系が停止したと判定 (… 最初の失敗から N 秒)` が出るまでの時間を見る。
- 既定は「1 秒ごと・タイムアウト 1 秒・3 回連続失敗」。`--fail-count` を変えて、検知時間と誤検知のかねあいを見る。

### E2 ハングアップを検知する

```sh
experiments/failover/chaos.sh hang primary     # 終わったら resume primary
```

- プロセスは生きていて TCP のポートも開いているが、API が応答しない状態。ポートが開いているかだけを見る監視（L4 のヘルスチェック）では見逃すことを確かめる。
- watchdog は API のタイムアウトで検知する。E1 との検知時間の差を記録する。

### E3 Standby 系の停止と追いつき

```sh
experiments/failover/chaos.sh kill standby
# 60 秒ほど待つ
experiments/failover/chaos.sh start standby
python3 experiments/failover/tools/check_history.py --minutes 5
```

- watchdog の `tx`（トランザクション番号）と `遅れ` が、再接続後に追いつくか。
- `check_history.py` の「TM パケットの差」が 0 になるか（止まっていた間の TM が Standby 系にも入ったか）。

### E4 承認つきの切替

```sh
python3 tools/send_command.py NO_OP --url http://localhost:8190 --instance m5sat   # 切替前に primary から1つ送っておく (E8 で使う)
experiments/failover/chaos.sh kill primary
python3 experiments/failover/tools/failover.py          # y で承認
python3 tools/send_command.py NO_OP --url http://localhost:8191 --instance m5sat   # 新 Active 系から送る
```

- `failover.py` が出す各ステップの時刻と合計時間（`logs/failover_*.json`）を記録する。
- 新 Active 系から送った NO_OP が `Verifier_Complete` まで進むか（地上局の ACK が新 Active 系に届くか）。
- 4-0 で Standby 系の ReplicationSlave が止まるか（状態が `RUNNING` 以外になるか）。止められなければ `failover.py` は昇格せずに中止する。
- YAMCS 単体で、Standby 系は再起動なしに「レプリケーションを止めて、リンクを有効にするだけ」で Active になれるか。

### E5 応答しない旧 Active 系の隔離

```sh
experiments/failover/stop.sh --clean && experiments/failover/start.sh
experiments/failover/chaos.sh hang primary
python3 experiments/failover/tools/failover.py --yes
```

- 2-a（API で TC リンクを無効化）がタイムアウトし、2-c（プロセスの停止。サーバの電源を切るのに相当）に切り替わって、3 の確認を通ってから 4 へ進むか。

### E6 二重送信の検知（わざと危険な状態をつくる）

```sh
experiments/failover/stop.sh --clean && experiments/failover/start.sh
python3 experiments/failover/tools/failover.py --yes --skip-fence   # 隔離せずに昇格 (実験専用)
python3 tools/send_command.py NO_OP --url http://localhost:8190 --instance m5sat
python3 tools/send_command.py NO_OP --url http://localhost:8191 --instance m5sat
```

- watchdog に `CRITICAL 両方の系の TC リンクが有効` が、`logs/fake_sat_events.jsonl` に `接続先でない primary 系から TC が届いた` が出るか。
- HK の `AcceptedCommands` が 2 つ増える（衛星が両方を受け付けてしまった）ことも確かめる。

### E7 隔離した系の自動再起動

```sh
experiments/failover/stop.sh --clean && experiments/failover/start.sh
python3 experiments/failover/tools/failover.py --yes        # primary の TC・TM を API で無効化して切替
experiments/failover/chaos.sh restart primary               # 落ちたプロセスが自動で起動しなおしたのに相当
# 起動したら
curl -s http://localhost:8190/api/links/m5sat/sat-tc
```

- primary の設定は `enabledAtStartup` を書いていないので、YAMCS の説明どおりなら前回の状態（無効）を引き継ぐはず。`"disabled": true` のままか。
- 有効に戻ってしまうなら、隔離した系は自動で起動しなおさないようにしておく必要がある。

### E8 コマンド履歴の複製

E4 のあとに:

```sh
python3 experiments/failover/tools/check_history.py --minutes 15
```

- 切替前に primary から送った NO_OP が、standby のコマンド履歴にあるか（`cmdhist_realtime` を複製している）。
- 「運用を再開する前に、それまでに送ったコマンドを確かめる」が、Standby 系だけでできるかがわかる。

### E9 旧 Active 系の復帰による流れ込み

昇格のあとも Standby 系のレプリケーションが動いていると、旧 Active 系が復帰して TM を受けたとき、そのデータが新 Active 系に流れ込む。`failover.py` は 4-0 でこれを止める。止めた場合と止めなかった場合を比べる。

```sh
# (a) 止めた場合: E4 のあとに続けて
experiments/failover/chaos.sh start primary                       # 旧 Active 系が復帰
curl -s -X POST http://localhost:8190/api/links/m5sat/sat-tm:enable   # 旧 Active 系が TM を受けられる状態にする
echo primary > experiments/failover/.run/target                   # 地上局がまちがって旧 Active 系へ TM を送る
# 30 秒ほど待って
echo standby > experiments/failover/.run/target
curl -s http://localhost:8191/api/replication                     # 新 Active 系がスレーブとして接続していないか

# (b) 止めなかった場合: stop.sh --clean && start.sh のあと、failover.py を使わずにリンクだけ有効にする
curl -s -X POST http://localhost:8191/api/links/m5sat/sat-tm:enable
curl -s -X POST http://localhost:8191/api/links/m5sat/sat-tc:enable
# 以降は (a) と同じ手順で、旧 Active 系に TM を送る
```

- (a) で、旧 Active 系が受けた TM が新 Active 系のアーカイブに入っていないか（`check_history.py` のパケット数）、`/api/replication` の slaves が空か。
- (b) では、旧 Active 系が受けた TM が新 Active 系にも入ってくるはず。watchdog は「Standby 系が TC 有効 (昇格済み) なのに、レプリケーションが動いたまま」を WARNING で出す。
- **再起動で元に戻ることも確かめる**: 新 Active 系（standby）を `chaos.sh restart standby` で再起動すると、ReplicationSlave は設定どおりにまた動き出すはず（止めたサービスの状態は残らない）。watchdog に `CRITICAL どちらの系の TC リンクも有効でない` が出て、standby の API が戻っても残るかも見る。

## 結果の記録

2026-11-06、Mac 1 台（YAMCS 5.13.0）で試した結果です。

| ID | 結果 | 数値 | メモ |
| --- | --- | --- | --- |
| E1 | ✅ | 最初の失敗から 2.0 秒（止めてから約 3 秒） | 1 秒ごと × 3 回連続の失敗。`Connection refused` |
| E2 | ✅ | 最初の失敗から 2.0 秒 | ポート 8190 は開いたまま（ポートだけ見る監視では見逃す）。API のタイムアウトで検知 |
| E3 | ✅ | パケットの差 0 | Standby 系を約 70 秒止めて戻すと、止まっていた間の TM も追いついた |
| E4 | ✅ | 切替 0.34 秒 / NO_OP は Complete まで進んだ | 再起動なしで昇格できた（レプリケーションを止めて、リンクを有効にするだけ） |
| E5 | ✅ | 切替 2.39 秒 | 2-a が 2 秒でタイムアウト → 2-c でプロセスを止め、3 の確認を通って 4 へ |
| E6 | ✅ | 3 か所で検知 | 地上局（接続先でない系から TC・30 秒以内に両方から TC）と watchdog（両方の TC リンクが有効）。衛星は両方の NO_OP を実行した |
| E7 | ✅ | 再起動後の TC リンク: 無効 | `enabledAtStartup` を書かなければ、前回の状態（無効）を引き継ぐ |
| E8 | ✅ | 複製されていないコマンド 0 件 | 切替の前に primary から送った NO_OP が、standby の履歴にあった |
| E9 | ⚠️ | 流れ込んだパケット 0 個 → 再起動後 30 個 / 再起動後のスレーブ: 動いた | (a) 止めている間は流れ込まない。ただし新 Active 系を再起動すると Standby 系に戻り、どちらも衛星と話さなくなる。watchdog に足した警告で見つけられる（下の「E9 で起きたこと」） |

## わかったこと

### 切替の手順で気をつけること

- **昇格は、新 Active 系を再起動すると元に戻る**（E9）。下の「E9 で起きたこと」を見てください
- **隔離した系のリンクの無効は、保存データに残る**（E7）。再起動しても無効のままだった。ただし、保存データを消して作り直す（`stop.sh --clean` や、サーバの入れ替え）と、設定どおり有効で起動する。隔離した系は、自動で起動しなおさないようにしておくほうが安全
- **二重送信が起きると、旧 Active 系ではコマンドが失敗に見える**（E6）。衛星は旧 Active 系からの NO_OP も実行したのに、ACK は地上局の接続先（新 Active 系）に返るので、旧 Active 系では `Verifier_Complete` が TIMEOUT になった。運用者が「失敗した」と思って送り直すと、もう一度実行されてしまう
- 検知は 1 秒ごと × 3 回で約 2〜3 秒、切替は承認のあと 1 秒以内（隔離に API のタイムアウトが入ると +2 秒）。全体の時間は、ほとんどが人の承認の時間になる

### E9 で起きたこと

切替（`failover.py`）は、Standby 系の**動いている間の状態だけ**を変える。レプリケーション（写しの受け取り）を API で止め、地上局とのリンクを API で有効にする。設定ファイルには、Standby 系のまま「写しを受け取る」「リンクは起動時に無効」と書いてある。

YAMCS は起動のときに設定ファイルを読むので、新 Active 系（standby）を再起動すると、Standby 系に戻った。

| | 切替の前 | 切替のあと | standby を再起動したあと |
| --- | --- | --- | --- |
| primary | Active（衛星と話す） | 隔離（衛星と話さない） | 隔離のまま |
| standby | Standby（控え） | **Active**（衛星と話す） | **Standby（控え）に戻った** |

- **どちらも衛星と話していない状態になった**。primary は隔離したまま、standby はリンクが無効に戻った。TM はどちらにも入らず、コマンドもどちらからも送れない
- **使っていない旧系のデータが流れ込んだ**。控えに戻った standby は、写しの受け取りを始め、primary がその間に受けていた TM（30 個。地上局がまちがって primary に送ったもの）を取り込んだ
- **はじめの watchdog では気づけなかった**。どちらのプロセスも動いていて API も応答するので、「両方とも正常」に見えた（見ていたのは「両方の TC リンクが有効」だけ）。そこで、下の解決策 3 を watchdog に足した

再起動の前（E9 の (a)）は、レプリケーションを止めていたので、旧系のデータは流れ込まなかった（0 個）。

**解決策**

本物の地上システムでよく使うやり方。1・2 は YAMCS の機能というより、YAMCS の外の作り方の話なので、この実験では試していない。3 は watchdog に足して確かめた。

1. **「今どちらが主系か」を 1 か所に置き、起動のときに必ずそこを見る**。役割を各系の設定ファイルに書かず、外の 1 か所（共有のファイル、データベース、管理サーバなど）に置く。起動した系は、それを見て Active としてふるまうか、控えになるかを決める。切替はその 1 か所を書きかえることなので、再起動しても役割は変わらない
2. **切替の手順に、設定の書きかえを入れる**。自動にしない場合は、手順の最後に「新しい主系の設定を書きかえる（ReplicationSlave を外し、リンクを有効で起動する）」「旧主系は自動で起動しないようにする」を入れる。小さな地上局では、これがいちばん多い
3. **「どちらも衛星と話していない」を監視で見つける**。「両方が話している」（二重送信）と同じくらい危ないので、両方を見張る。1・2 をしていても、まちがいに気づくための最後の守りになる。
   watchdog は、どちらの系の TC リンクも有効でない（止まっている系も含む）状態が 3 回（3 秒）続いたら、`CRITICAL どちらの系の TC リンクも有効でない (衛星と話している系がない)` を出す。切替の途中は一瞬どちらも無効になるので、すぐには出さない。
   **確かめたこと**：切替の途中・あとには出ず、standby を再起動すると、standby が止まったと判定したのと同じ時刻に出た。standby の API が戻っても解消されずに残った（控えに戻ったことがわかる）。Active 系が落ちたとき（E1）も、切替するまでこの警告が出る

### 同じ Mac で 2 つの系を動かすときの落とし穴

本番の構成では起きないが、実験で引っかかったこと。

- **`serverId` を系ごとに変えないと、レプリケーションが何も届かない**。YAMCS はインスタンスの番号を「サーバ名 + インスタンス名」から作る。サーバ名の既定はホスト名なので、同じ Mac で同じインスタンス名（`m5sat`）だと番号が同じになり、Standby 系は受け取ったデータを「自分が出したもの」として捨てる（トランザクション番号だけは進むので、気づきにくい）。`yamcs.yaml` に `serverId` を書いた
- **YAMCS の JVM は `mvnw` とは別のプロセス**。`yamcs:run` が起動する JVM のコマンドラインには `failover/primary` が入らないので、`pkill -f` では `mvnw` だけが止まり、YAMCS は動き続けた。`chaos.sh`・`stop.sh`・`failover.py` は、待ち受けているポート（8190 / 8191）からも JVM を探すようにした
- **地上局の二重送信の見張りが、正しい切替でも鳴った**（E4）。切替の直前に旧 Active 系から送った TC と、直後に新 Active 系から送った TC が 30 秒以内だったため。接続先を切り替えたら、それまでの TC の時刻を忘れるようにした（切替のあとに旧 Active 系から届いたものは「接続先でない系から」で見つかる）

## 限界（この実験でわからないこと）

- Mac 1 台の中で 2 つの系を動かしているので、離れた場所どうしのネットワークの遅れや、Active 系と Standby 系の間のネットワークだけが切れる分断は再現していません。
- ここで確かめるのは YAMCS 標準のレプリケーション（ReplicationMaster / ReplicationSlave）です。アーカイブを外のデータベースに置いて両方の系から使う形とは別物です。

## 参考

- [Replication Master](https://docs.yamcs.org/yamcs-server-manual/services/instance/replication-master/) / [Replication Slave](https://docs.yamcs.org/yamcs-server-manual/services/instance/replication-slave/) / [Replication Server](https://docs.yamcs.org/yamcs-server-manual/services/global/replication-server/)
- [Links（enabledAtStartup）](https://docs.yamcs.org/yamcs-server-manual/links/)
- HTTP API: [List Services](https://docs.yamcs.org/yamcs-http-api/services/list-services/) / [Stop Service](https://docs.yamcs.org/yamcs-http-api/services/stop-service/) / [Get Link](https://docs.yamcs.org/yamcs-http-api/links/get-link/) / [Disable Link](https://docs.yamcs.org/yamcs-http-api/links/disable-link/) / [Get Replication Info](https://docs.yamcs.org/yamcs-http-api/replication/get-replication-info/)
