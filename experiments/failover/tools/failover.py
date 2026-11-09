#!/usr/bin/env python3
"""Active 系 (primary) から Standby 系 (standby) への切替手順。

  1 状況を表示して、運用者の承認を待つ (検知は自動、切替は人が決める)
  2 旧 Active 系の隔離: TC・TM リンクを API で無効化する。API が応答しなければ、
     プロセスを止める (サーバの電源を切るのにあたる)
  3 隔離の確認: 旧 Active 系の TC リンクが無効、またはプロセスが止まっていることを確かめる。
     確かめられなければ、ここで中止する (4 へ進まない)
  4 昇格: まず Standby 系のレプリケーション (ReplicationSlave) を止める。止めないと、
     旧 Active 系が復帰して TM を受け続けたとき、そのデータが新 Active 系に流れ込む。
     止められなければ、ここで中止する。そのあと Standby 系の TC・TM リンクを有効にし、
     地上局の接続先を standby に切り替える
  5 再開の前に確認すること (コマンド履歴など) を表示する

各ステップの所要時間を測って logs/failover_*.json に残す。

使い方:
    python3 tools/failover.py               # 承認を対話で聞く
    python3 tools/failover.py --yes         # 承認を省く (所要時間の計測用)
    python3 tools/failover.py --skip-fence  # 【危険・実験 E6 専用】隔離をせずに昇格する
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

import yamcs_api as api

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.dirname(HERE)


class Timeline:
    def __init__(self):
        self.t0 = time.time()
        self.steps = []

    def step(self, name, ok, detail=""):
        t = time.time() - self.t0
        self.steps.append({"step": name, "ok": ok, "seconds": round(t, 3), "detail": detail})
        mark = "OK " if ok else "NG "
        print(f"  [{t:6.2f}s] {mark}{name}{(': ' + detail) if detail else ''}", flush=True)


def primary_pids():
    """旧 Active 系の YAMCS のプロセス (start.sh が起動したもの)"""
    # mvnw と、yamcs:run が別に起動した YAMCS の JVM。JVM のコマンドラインには failover/primary が
    # 入らないので、待ち受けているポート (8190) から探す
    out = subprocess.run(["pgrep", "-f", "failover/primary"], capture_output=True, text=True).stdout
    out += subprocess.run(["lsof", "-t", "-nP", "-iTCP:8190", "-sTCP:LISTEN"], capture_output=True, text=True).stdout
    return sorted({int(p) for p in out.split() if int(p) != os.getpid()})


def status(node):
    try:
        tc = api.link(node, api.TC_LINK)
        tm = api.link(node, api.TM_LINK)
        return f"応答あり TC={'無効' if tc.get('disabled') else '有効'} TM={'無効' if tm.get('disabled') else '有効'}"
    except api.ApiError as e:
        return f"応答なし ({e})"


def fence(tl, args):
    """2 隔離。まず API で TC→TM の順に無効化し、だめならプロセスを止める"""
    try:
        api.set_link("primary", api.TC_LINK, False, timeout=args.timeout)
        tl.step("2-a 旧Active系の TC リンクを無効化 (API)", True)
        try:
            api.set_link("primary", api.TM_LINK, False, timeout=args.timeout)
            tl.step("2-b 旧Active系の TM リンクを無効化 (API)", True)
        except api.ApiError as e:
            tl.step("2-b 旧Active系の TM リンクを無効化 (API)", False, str(e))
        return
    except api.ApiError as e:
        tl.step("2-a 旧Active系の TC リンクを無効化 (API)", False, str(e))
    pids = primary_pids()
    if not pids:
        tl.step("2-c 旧Active系のプロセスを停止", True, "プロセスはすでにない")
        return
    for pid in pids:
        try:
            os.kill(pid, signal.SIGKILL)  # SIGSTOP で止まっていても SIGKILL は効く
        except ProcessLookupError:
            pass
    tl.step("2-c 旧Active系のプロセスを停止 (電源を切るのに相当)", True, f"pid {pids}")


def verify_fence(tl, args):
    """3 隔離の確認。TC リンクが無効と返ってくるか、プロセスがないこと"""
    deadline = time.time() + args.verify_seconds
    while time.time() < deadline:
        try:
            if api.link("primary", api.TC_LINK, timeout=args.timeout).get("disabled"):
                tl.step("3 隔離の確認", True, "旧Active系の TC リンクは無効")
                return True
        except api.ApiError:
            if not primary_pids():
                tl.step("3 隔離の確認", True, "旧Active系のプロセスは止まっている")
                return True
        time.sleep(0.2)
    tl.step("3 隔離の確認", False, "TC 無効もプロセス停止も確かめられない")
    return False


def stop_replication(tl, args):
    """4-0 Standby 系の ReplicationSlave を止める。止まったと確かめられたら True"""
    service = api.find_service("standby", api.REPLICATION_SLAVE_CLASS, timeout=args.timeout)
    if service is None:
        tl.step("4-0 Standby系のレプリケーションを停止", True, "ReplicationSlave が設定されていない")
        return True
    if service.get("state") == "RUNNING":
        api.stop_service("standby", service["name"], timeout=args.timeout)
    deadline = time.time() + 5
    while time.time() < deadline:
        service = api.find_service("standby", api.REPLICATION_SLAVE_CLASS, timeout=args.timeout)
        if service is None or service.get("state") != "RUNNING":
            state = service.get("state") if service else "なし"
            tl.step("4-0 Standby系のレプリケーションを停止", True, f"{service['name'] if service else ''} の状態: {state}")
            return True
        time.sleep(0.2)
    tl.step("4-0 Standby系のレプリケーションを停止", False, "5 秒待っても RUNNING のまま")
    return False


def promote(tl, args):
    """4 昇格。Standby 系はレプリケーションで受け取ったデータを持ったまま、
    レプリケーションを止めてから、リンクを有効にする"""
    if not stop_replication(tl, args):
        raise api.ApiError("レプリケーションを止められないので、昇格しない")
    api.set_link("standby", api.TM_LINK, True, timeout=args.timeout)
    tl.step("4-a Standby系の TM リンクを有効化", True)
    with open(os.path.join(args.run_dir, "target"), "w") as f:
        f.write("standby\n")
    tl.step("4-b 地上局の接続先を standby に切替", True)
    api.set_link("standby", api.TC_LINK, True, timeout=args.timeout)
    tl.step("4-c Standby系の TC リンクを有効化", True)
    # 地上局からの TM が新 Active 系に届き始めるまで待つ
    before = int(api.link("standby", api.TM_LINK).get("dataInCount", 0))
    deadline = time.time() + 10
    while time.time() < deadline:
        if int(api.link("standby", api.TM_LINK).get("dataInCount", 0)) > before:
            tl.step("4-d 新Active系に地上局の TM が届いた", True)
            return
        time.sleep(0.2)
    tl.step("4-d 新Active系に地上局の TM が届いた", False, "10 秒待っても届かない")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--yes", action="store_true", help="承認の確認を省く")
    parser.add_argument("--skip-fence", action="store_true", help="【危険】2・3を飛ばす。実験 E6 (二重送信の検知) 専用")
    parser.add_argument("--timeout", type=float, default=2.0, help="API 呼び出しのタイムアウト (秒)")
    parser.add_argument("--verify-seconds", type=float, default=10.0, help="3で待つ最大の秒数")
    parser.add_argument("--run-dir", default=os.path.join(BASE, ".run"))
    parser.add_argument("--log-dir", default=os.path.join(BASE, "logs"))
    args = parser.parse_args()

    print("1 現在の状態")
    print(f"  primary: {status('primary')}")
    print(f"  standby: {status('standby')}")
    if not args.yes:
        answer = input("Standby 系へ切り替えますか? 旧 Active 系の TC を止めてから昇格します [y/N] ")
        if answer.strip().lower() != "y":
            print("中止しました")
            return 1
    tl = Timeline()
    tl.step("1 承認", True, "--yes" if args.yes else "運用者が承認")

    if args.skip_fence:
        tl.step("2・3 隔離", False, "【実験 E6】隔離を飛ばした。二重送信が起きうる状態")
    else:
        fence(tl, args)
        if not verify_fence(tl, args):
            print("隔離を確かめられないので、昇格せずに中止します (手順 3)")
            save(tl, args, aborted=True)
            return 2

    try:
        promote(tl, args)
    except api.ApiError as e:
        tl.step("4 昇格", False, str(e))
        save(tl, args, aborted=True)
        return 3

    print("5 再開の前に確認すること")
    print("  - python3 tools/check_history.py で、旧 Active 系で送ったコマンドが Standby 系の履歴にあるか")
    print("  - Web UI (http://localhost:8191) で HK が更新され続けているか")
    print("  - 古いスケジュール・自動化が二重に動かないか (この実験では Timeline を使っていない)")
    print("  - 注意: 止めたレプリケーションは、Standby 系の YAMCS を再起動すると設定どおりにまた動き出す。")
    print("    切替が長引くなら、Standby 系の設定から ReplicationSlave を外す (切り戻しの計画作業で戻す)")
    save(tl, args, aborted=False)
    return 0


def save(tl, args, aborted):
    os.makedirs(args.log_dir, exist_ok=True)
    path = os.path.join(args.log_dir, time.strftime("failover_%Y%m%d_%H%M%S.json"))
    with open(path, "w") as f:
        json.dump({"started": tl.t0, "aborted": aborted, "skipFence": args.skip_fence, "steps": tl.steps}, f,
                  ensure_ascii=False, indent=2)
    print(f"記録: {path}  (合計 {time.time() - tl.t0:.2f} 秒)")


if __name__ == "__main__":
    sys.exit(main())
