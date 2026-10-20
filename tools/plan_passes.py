#!/usr/bin/env python3
"""YAMCS の Timeline に、パス (衛星と通信できる時間) と観測の計画を作る。

使い方:
    python3 tools/plan_passes.py create                 # 2分後から、10分ごとに2分間のパスを3回
    python3 tools/plan_passes.py create --passes 5 --interval 6 --duration 2
    python3 tools/plan_passes.py list                   # 作った計画を表示する
    python3 tools/plan_passes.py clear                  # これからの予定を消し、データリンクを常に有効へ戻す
    python3 tools/plan_passes.py clear --all            # 終わった予定も含めて全部消す

本物の運用では、衛星の軌道と地上局の場所からパスの時刻を予測する。M5Sat は軌道を回っていないので、
決まった間隔の人工のパスを作る。

作るもの:
- パス (EVENT): 通信できる時間帯
- AOS / LOS (アクティビティ): パスの始めにデータリンクを有効、終わりに無効にする (etc/scripts/set_links.py)
- 通信の確認 (アクティビティ): AOS でテレメトリが届き始めたら、スタック 01-link-check.ycs を自動で実行する
- 観測の予定 (EVENT): パスとパスの間の観測。パスの間に、運用者が SCHEDULE_OBSERVATION で予約する
- 帯 (バンド) とビュー「パス運用」: 初めて create したときに作る

予定・帯・ビューは YAMCS のデータベースに保存される (git では管理できない) ので、このスクリプトで作り直せるようにしている。
"""

import argparse
import datetime
import json
import sys
import urllib.error
import urllib.parse
import urllib.request

TAG = "pass-plan"  # このスクリプトで作った予定に付けるタグ (clear で消す目印)
VIEW_NAME = "パス運用"

PASS_COLOR = "#8fd19e"
OBSERVATION_COLOR = "#a9c8f0"
LINK_COLOR = "#d9d9d9"


def request(args, path, body=None, method=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"{args.url}/api{path}", data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req) as res:
            text = res.read()
            return json.loads(text) if text else {}
    except urllib.error.HTTPError as e:
        sys.exit(f"{method or ('POST' if data else 'GET')} {path}: {e.code} {e.read().decode()}")


def iso(t):
    return t.astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")


def jst(t):
    return t.astimezone(datetime.timezone(datetime.timedelta(hours=9))).strftime("%H:%M:%S")


def save_item(args, **item):
    item.setdefault("tags", [])
    item["tags"] = [TAG] + item["tags"]
    return request(args, f"/timeline/{args.instance}/items", {"source": "rdb", **item})


def script_activity(script_args):
    return {"type": "SCRIPT", "args": {"script": "set_links.py", "args": script_args, "processor": "realtime"}}


# --- 帯とビュー ---------------------------------------------------------------

def value_mappings(mappings):
    props = {}
    for i, (value, color) in enumerate(mappings):
        props[f"value_mapping_{i}_type"] = "value"
        props[f"value_mapping_{i}_value"] = value
        props[f"value_mapping_{i}_color"] = color
    return props


BANDS = [
    {"name": "UTC", "type": "TIME_RULER", "properties": {"timezone": "UTC"}},
    {"name": "日本時間", "type": "TIME_RULER", "properties": {"timezone": "Asia/Tokyo"}},
    {"name": "パス", "type": "ITEM_BAND", "tags": ["pass"]},
    {"name": "通信と手順", "type": "ITEM_BAND", "tags": ["link", "procedure"]},
    {"name": "観測の予定", "type": "ITEM_BAND", "tags": ["observation"]},
    {"name": "送ったコマンド", "type": "COMMAND_BAND"},
    {"name": "運用モード", "type": "PARAMETER_STATES",
     "properties": {"parameter": "/M5Sat/Bus/Mode",
                    **value_mappings([("SAFE", "#f5c542"), ("NOMINAL", "#8fd19e"), ("MISSION", "#7fb2f0")])}},
    {"name": "観測の状態", "type": "PARAMETER_STATES",
     "properties": {"parameter": "/M5Sat/Bus/ObservationState",
                    **value_mappings([("NONE", "#eeeeee"), ("SCHEDULED", "#f0d98f"), ("ACTIVE", "#7fb2f0")])}},
    {"name": "最後の観測の結果", "type": "PARAMETER_STATES",
     "properties": {"parameter": "/M5Sat/Bus/LastObservationResult",
                    **value_mappings([("COMPLETED", "#8fd19e"), ("ABORTED", "#f5c542"),
                                      ("SKIPPED_PAYLOAD_OFF", "#f5c542"), ("SKIPPED_NOT_NOMINAL", "#f5c542"),
                                      ("CANCELLED", "#cccccc")])}},
    {"name": "ペイロード電源", "type": "PARAMETER_STATES",
     "properties": {"parameter": "/M5Sat/Bus/PayloadPower",
                    **value_mappings([("true", "#8fd19e"), ("false", "#bbbbbb")])}},
]


def ensure_view(args):
    """帯とビュー「パス運用」を用意する。すでにあれば、足りない帯を足して並び順をそろえる"""
    existing = {b["name"]: b["id"] for b in request(args, f"/timeline/{args.instance}/bands").get("bands", [])}
    band_ids = []
    for band in BANDS:
        if band["name"] not in existing:
            body = {"shared": True, **band}
            if band["type"] == "ITEM_BAND":
                body["source"] = "rdb"
            existing[band["name"]] = request(args, f"/timeline/{args.instance}/bands", body)["id"]
        band_ids.append(existing[band["name"]])
    body = {"name": VIEW_NAME, "description": "パスと観測の計画、実行した手順、衛星の状態の移り変わり",
            "bands": band_ids}
    views = request(args, f"/timeline/{args.instance}/views").get("views", [])
    view = next((v for v in views if v["name"] == VIEW_NAME), None)
    if view:
        request(args, f"/timeline/{args.instance}/views/{view['id']}", body, method="PUT")
    else:
        request(args, f"/timeline/{args.instance}/views", body)
        print(f"ビュー「{VIEW_NAME}」を作りました")


# --- 計画 ---------------------------------------------------------------------

def create(args):
    ensure_view(args)
    now = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
    first = now + datetime.timedelta(minutes=args.start)
    interval = datetime.timedelta(minutes=args.interval)
    duration = datetime.timedelta(minutes=args.duration)
    if duration >= interval:
        sys.exit("--duration は --interval より短くしてください")

    # 計画を始めた時点はパスの外なので、まず通信を止める
    save_item(args, type="ACTIVITY", name="LOS (計画の開始)", start=iso(now + datetime.timedelta(seconds=5)),
              duration="5s", tags=["link"], autoStart=True, activityDefinition=script_activity("disable"),
              properties={"backgroundColor": LINK_COLOR})

    print(f"{'#':>2}  {'AOS (UTC)':<10}{'LOS (UTC)':<10}{'日本時間':<20}観測の予定 (SCHEDULE_OBSERVATION)")
    for k in range(args.passes):
        aos = first + interval * k
        los = aos + duration
        n = k + 1
        save_item(args, type="EVENT", name=f"パス {n}", start=iso(aos), duration=f"{int(duration.total_seconds())}s",
                  tags=["pass"], properties={"backgroundColor": PASS_COLOR})
        aos_item = save_item(args, type="ACTIVITY", name=f"AOS {n}", start=iso(aos), duration="5s", tags=["link"],
                             autoStart=True, activityDefinition=script_activity("enable"),
                             properties={"backgroundColor": LINK_COLOR})
        # AOS (テレメトリが届き始めた) が成功したら、手順を始める
        save_item(args, type="ACTIVITY", name=f"通信の確認 {n}", duration="20s", tags=["procedure"], autoStart=True,
                  activityDefinition={"type": "STACK", "args": {"bucket": "stacks", "stack": "01-link-check.ycs",
                                                                "processor": "realtime"}},
                  predecessors=[{"itemId": aos_item["id"], "startCondition": "ON_SUCCESS"}])
        save_item(args, type="ACTIVITY", name=f"LOS {n}", start=iso(los), duration="5s", tags=["link"],
                  autoStart=True, activityDefinition=script_activity("disable"),
                  properties={"backgroundColor": LINK_COLOR})

        obs = ""
        if k < args.passes - 1:
            # このパスと次のパスの間に観測する。予約はこのパスの間に送る
            gap = interval - duration
            obs_start = los + gap / 4
            obs_len = int((gap / 2).total_seconds())
            epoch = int(obs_start.timestamp())
            save_item(args, type="EVENT", name=f"観測 {n}", start=iso(obs_start), duration=f"{obs_len}s",
                      tags=["observation"], properties={"backgroundColor": OBSERVATION_COLOR},
                      description=f"パス {n} の間に予約する: SCHEDULE_OBSERVATION Start={iso(obs_start)} "
                                  f"Duration={obs_len}")
            obs = f"Start={iso(obs_start)} Duration={obs_len}"
        print(f"{n:>2}  {aos:%H:%M:%S}  {los:%H:%M:%S}  {jst(aos)}〜{jst(los)}  {obs}")

    print(f"\n最後のパスのあとは通信が止まったままです。終わったら `python3 tools/plan_passes.py clear` で常時通信に戻します")


def list_items(args):
    items = plan_items(args)
    for item in sorted(items, key=lambda i: i["start"]):
        status = item.get("status", "")
        print(f"{item['start']}  {item['name']:<20} {status}")
    if not items:
        print("計画はありません")


def plan_items(args):
    query = urllib.parse.urlencode({"source": "rdb", "limit": 500})
    items = request(args, f"/timeline/{args.instance}/items?{query}").get("items", [])
    return [i for i in items if TAG in i.get("tags", [])]


def clear(args):
    """これからの予定を消し、データリンクを有効に戻す。終わった予定は振り返りのために残す (--all で全部消す)"""
    now = datetime.datetime.now(datetime.timezone.utc)
    items = plan_items(args)
    for item in items:
        if item.get("status") in ("IN_PROGRESS", "READY", "WAITING_ON_DEPENDENCY"):
            request(args, f"/timeline/{args.instance}/items/{item['id']}:cancelActivity", {})
    removed = 0
    for item in items:
        start = item.get("start")
        future = start is None or datetime.datetime.fromisoformat(start.replace("Z", "+00:00")) > now
        if args.all or future:
            quoted = urllib.parse.quote(item["id"], safe="")
            request(args, f"/timeline/{args.instance}/items/{quoted}?source=rdb", method="DELETE")
            removed += 1
    for link in ["m5stack-gnss", "m5stack-tc"]:
        request(args, f"/links/{args.instance}/{link}:enable", {})
    kept = len(items) - removed
    print(f"予定を {removed} 件消し、データリンクを有効に戻しました (常時通信)")
    if kept:
        print(f"終わった予定 {kept} 件は、振り返りのために残しています (全部消すときは clear --all)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", choices=["create", "list", "clear"])
    parser.add_argument("--passes", type=int, default=3, help="パスの回数 (default: 3)")
    parser.add_argument("--interval", type=float, default=10, help="パスの間隔 [分] (default: 10)")
    parser.add_argument("--duration", type=float, default=2, help="パスの長さ [分] (default: 2)")
    parser.add_argument("--start", type=float, default=2, help="最初のパスまでの時間 [分] (default: 2)")
    parser.add_argument("--url", default="http://localhost:8090", help="YAMCSのURL")
    parser.add_argument("--instance", default="gnss")
    parser.add_argument("--all", action="store_true", help="clear: 終わった予定も含めて全部消す")
    args = parser.parse_args()
    {"create": create, "list": list_items, "clear": clear}[args.action](args)


if __name__ == "__main__":
    main()
