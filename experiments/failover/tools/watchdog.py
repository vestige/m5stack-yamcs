#!/usr/bin/env python3
"""死活監視。

両方の系の外から 1 秒ごとに YAMCS の HTTP API を見て、次を判定する。判定するだけで、切り替えはしない
(検知は自動、切替は運用者の承認。tools/failover.py)。

  - HTTP API が応答するか (連続 FAIL_COUNT 回失敗で「停止」と判定)
  - TC リンクの状態。両方の系の TC リンクが有効なら CRITICAL (二重送信のおそれ)。
    どちらの系の TC リンクも有効でない (止まっている系も含む) 状態が FAIL_COUNT 回続いたら CRITICAL
    (衛星と話している系がない。切替の途中は一瞬そうなるので、すぐには出さない)
  - レプリケーションの遅れ: Active 系と Standby 系の HK の Uptime の差 (ms)、
    GET /api/replication のトランザクション番号
  - 模擬地上局 (fake_sat.py) が見つけた二重送信の警告

画面に 1 秒ごとの状態を出し、状態が変わったときだけ logs/watchdog_events.jsonl に残す。
検知にかかった時間は「最初に失敗した時刻」と「停止と判定した時刻」の両方を残すので、あとで比べられる。

使い方:
    python3 tools/watchdog.py                  # Ctrl+C で終わる
    python3 tools/watchdog.py --fail-count 5   # 判定を慎重にする (誤検知と検知時間のかねあい)
"""

import argparse
import json
import os
import time

import yamcs_api as api

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.dirname(HERE)


class Watchdog:
    def __init__(self, args):
        self.args = args
        self.fail_streak = {n: 0 for n in api.NODES}
        self.first_fail = {n: None for n in api.NODES}
        self.down = {n: False for n in api.NODES}
        self.conditions = {}
        self.no_tc_streak = 0
        os.makedirs(args.log_dir, exist_ok=True)
        self.events_path = os.path.join(args.log_dir, "watchdog_events.jsonl")

    def event(self, level, message, **extra):
        t = time.time()
        stamp = time.strftime("%H:%M:%S", time.localtime(t)) + f".{int(t * 1000) % 1000:03d}"
        print(f"\n[{stamp}] {level:8s} {message}", flush=True)
        with open(self.events_path, "a") as f:
            f.write(json.dumps({"time": t, "level": level, "message": message, **extra}, ensure_ascii=False) + "\n")

    def condition(self, key, active, level, message, **extra):
        """状態が変わったときだけ記録する (同じ警告を毎秒出さない)"""
        if active and not self.conditions.get(key):
            self.event(level, message, **extra)
        elif not active and self.conditions.get(key):
            self.event("RECOVER", f"解消: {message}")
        self.conditions[key] = active

    def probe(self, node):
        info = {"alive": False}
        try:
            api.call(node, "", timeout=self.args.timeout)  # GET /api (サーバの情報)
            info["alive"] = True
            tc = api.link(node, api.TC_LINK, timeout=self.args.timeout)
            tm = api.link(node, api.TM_LINK, timeout=self.args.timeout)
            info["tcEnabled"] = not tc.get("disabled", False)
            info["tcStatus"] = tc.get("status")
            info["tmEnabled"] = not tm.get("disabled", False)
            info["tmIn"] = int(tm.get("dataInCount", 0))
            info["uptime"] = api.parameter(node, api.UPTIME_PARAMETER, timeout=self.args.timeout)
            rep = api.replication(node, timeout=self.args.timeout)
            masters = rep.get("masters", [])
            slaves = rep.get("slaves", [])
            if masters:
                info["masterTx"] = int(masters[0].get("nextTx", 0) or 0)
                info["slavesConnected"] = sum(1 for m in masters if m.get("remoteAddress"))
            if slaves:
                info["slaveTx"] = int(slaves[0].get("tx", 0) or 0)
                info["slaveConnected"] = bool(slaves[0].get("remoteAddress"))
            service = api.find_service(node, api.REPLICATION_SLAVE_CLASS, timeout=self.args.timeout)
            if service is not None:
                info["slaveState"] = service.get("state")
        except api.ApiError as e:
            info["error"] = str(e)
        return info

    def update_liveness(self, node, info):
        if info["alive"]:
            if self.down[node]:
                self.event("RECOVER", f"{node} 系の HTTP API が応答した", node=node)
            self.fail_streak[node] = 0
            self.first_fail[node] = None
            self.down[node] = False
            return
        if self.fail_streak[node] == 0:
            self.first_fail[node] = time.time()
        self.fail_streak[node] += 1
        if not self.down[node] and self.fail_streak[node] >= self.args.fail_count:
            self.down[node] = True
            elapsed = time.time() - self.first_fail[node]
            self.event("ALERT", f"{node} 系が停止したと判定 (連続 {self.fail_streak[node]} 回失敗、"
                       f"最初の失敗から {elapsed:.1f} 秒): {info.get('error')}",
                       node=node, firstFail=self.first_fail[node], detectSeconds=elapsed)

    def run(self):
        print(f"監視を開始: {api.NODES}  (間隔 {self.args.interval}s, タイムアウト {self.args.timeout}s, "
              f"{self.args.fail_count} 回連続失敗で停止と判定)")
        while True:
            started = time.time()
            infos = {n: self.probe(n) for n in api.NODES}
            for n, info in infos.items():
                self.update_liveness(n, info)
            p, s = infos["primary"], infos["standby"]

            both_tc = p.get("tcEnabled") and s.get("tcEnabled")
            self.condition("double-tc", bool(both_tc), "CRITICAL", "両方の系の TC リンクが有効 (二重送信のおそれ)")
            # 衛星と話している系がない: Active 系が落ちた、昇格した系が再起動して Standby 系に戻った (実験 E9) など
            no_tc = not (p.get("tcEnabled") or s.get("tcEnabled"))
            self.no_tc_streak = self.no_tc_streak + 1 if no_tc else 0
            self.condition("no-tc", self.no_tc_streak >= self.args.fail_count, "CRITICAL",
                           "どちらの系の TC リンクも有効でない (衛星と話している系がない)")

            lag = None
            if p.get("uptime") is not None and s.get("uptime") is not None:
                lag = p["uptime"] - s["uptime"]
                self.condition("lag", lag > self.args.lag_ms, "WARNING",
                               f"レプリケーションの遅れが {self.args.lag_ms} ms を超えた (Uptime の差)", lagMs=lag)
            promoted = bool(s.get("tcEnabled"))
            if s.get("alive") and "slaveConnected" in s and not promoted:
                self.condition("slave", not s["slaveConnected"], "WARNING", "Standby 系が Active 系に接続していない")
            # 昇格したのにレプリケーションが動いたまま: 旧 Active 系が復帰すると、そのデータが流れ込む
            self.condition("slave-after-promote", promoted and s.get("slaveState") == "RUNNING", "WARNING",
                           "Standby 系が TC 有効 (昇格済み) なのに、レプリケーションが動いたまま")

            status_path = os.path.join(self.args.run_dir, "fake_sat_status.json")
            try:
                with open(status_path) as f:
                    gs = json.load(f)
                self.condition("gs-alert", gs.get("alerts", 0) > 0, "CRITICAL",
                               f"地上局が二重送信の疑いを検知 (TC 受信数 {gs.get('tcCount')})")
                target = gs.get("target")
            except (FileNotFoundError, json.JSONDecodeError):
                target = "?"

            def fmt(name, i):
                if not i["alive"]:
                    return f"{name}:DOWN({self.fail_streak[name]})"
                tc = "TC有効" if i.get("tcEnabled") else "TC無効"
                tx = i.get("masterTx", i.get("slaveTx", "-"))
                return f"{name}:UP {tc} uptime={i.get('uptime')} tx={tx}"
            line = f"{time.strftime('%H:%M:%S')} {fmt('primary', p)} | {fmt('standby', s)} | 遅れ={"-" if lag is None else f"{lag}ms"} | 地上局の接続先={target}"
            print("\r" + line.ljust(150), end="", flush=True)
            time.sleep(max(0.0, self.args.interval - (time.time() - started)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--interval", type=float, default=1.0, help="監視の間隔 (秒)")
    parser.add_argument("--timeout", type=float, default=1.0, help="1 回の API 呼び出しのタイムアウト (秒)")
    parser.add_argument("--fail-count", type=int, default=3, help="何回連続で失敗したら停止と判定するか")
    parser.add_argument("--lag-ms", type=int, default=5000, help="レプリケーションの遅れの警告の閾値 (ms)")
    parser.add_argument("--run-dir", default=os.path.join(BASE, ".run"))
    parser.add_argument("--log-dir", default=os.path.join(BASE, "logs"))
    try:
        Watchdog(parser.parse_args()).run()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
