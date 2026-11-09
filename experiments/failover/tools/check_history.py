#!/usr/bin/env python3
"""両方の系のアーカイブを比べる (実験 E3・E8)。

  - 直近 MINUTES 分の TM パケットの数 (レプリケーションの取りこぼしがないか)
  - コマンド履歴: 旧 Active 系で送ったコマンドが、Standby 系にも複製されているか

応答しない系は「応答なし」と表示して、もう一方だけ見せる。

使い方:
    python3 tools/check_history.py              # 直近 10 分
    python3 tools/check_history.py --minutes 30
"""

import argparse
import datetime as dt

import yamcs_api as api


def iso(t):
    return t.strftime("%Y-%m-%dT%H:%M:%S.000Z")


def count_packets(node, start, stop):
    total, token = 0, None
    while True:
        q = f"?start={iso(start)}&stop={iso(stop)}&limit=1000" + (f"&next={token}" if token else "")
        res = api.call(node, f"/archive/{api.INSTANCE}/packets{q}", timeout=10)
        total += len(res.get("packet", []))
        token = res.get("continuationToken")
        if not token:
            return total


def commands(node, start, stop):
    q = f"?start={iso(start)}&stop={iso(stop)}&limit=200"
    res = api.call(node, f"/archive/{api.INSTANCE}/commands{q}", timeout=10)
    return [(c.get("generationTime"), c.get("commandName"), c.get("origin")) for c in res.get("commands", [])]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--minutes", type=float, default=10)
    args = parser.parse_args()
    stop = dt.datetime.now(dt.timezone.utc)
    start = stop - dt.timedelta(minutes=args.minutes)
    print(f"範囲: {iso(start)} 〜 {iso(stop)}")
    results = {}
    for node in api.NODES:
        try:
            results[node] = (count_packets(node, start, stop), commands(node, start, stop))
            print(f"{node}: TM パケット {results[node][0]} 個、コマンド履歴 {len(results[node][1])} 件")
        except api.ApiError as e:
            print(f"{node}: 応答なし ({e})")
    if len(results) == 2:
        diff = results["primary"][0] - results["standby"][0]
        print(f"TM パケットの差 (primary - standby): {diff}")
        p_cmds = {(t, n) for t, n, _ in results["primary"][1]}
        s_cmds = {(t, n) for t, n, _ in results["standby"][1]}
        missing = sorted(p_cmds - s_cmds)
        print(f"Standby 系に複製されていないコマンド: {len(missing)} 件")
        for t, n in missing[:20]:
            print(f"  {t} {n}")
    for node, (_, cmds) in results.items():
        print(f"--- {node} のコマンド履歴 (新しい順に最大 10 件)")
        for t, n, o in sorted(cmds, reverse=True)[:10]:
            print(f"  {t} {n} (origin={o})")


if __name__ == "__main__":
    main()
