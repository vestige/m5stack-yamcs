#!/usr/bin/env python3
"""異常対応の訓練 (5-10) を振り返る。出題 (tools/inject_fault.py) のあとに YAMCS に残った記録を、時系列の表にする。

使い方:
    python3 tools/training_report.py                 # 最後の出題を振り返る
    python3 tools/training_report.py --list          # これまでの出題の一覧
    python3 tools/training_report.py --index 2       # 一覧の 2 番目の出題を振り返る
    python3 tools/training_report.py --start 2026-10-20T08:40:40Z --kind gnss-stop   # 記録の無い時刻から振り返る

表に出すもの (YAMCS のアーカイブから):
- アラーム: 上がった時刻と、運用者が確認 (Acknowledge) した時刻
- イベント: FAULT_DETECTED、BOOT、MODE_CHANGED など
- コマンド: 運用者が送ったコマンドと結果
- 運用モードの移り変わり

あわせて「気づくまで」(運用者の最初の対応: アラームの確認か、運用者が送ったコマンド) と「復旧まで」の時間を出す。
"""

import argparse
import datetime
import json
import pathlib
import sys
import urllib.error
import urllib.parse
import urllib.request

LOG = pathlib.Path(__file__).resolve().parent.parent / "logs" / "training.jsonl"


def parse_time(text):
    return datetime.datetime.fromisoformat(text.replace("Z", "+00:00"))


def iso(t):
    return t.astimezone(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


def request(args, path, **query):
    url = f"{args.url}/api/archive/{args.instance}/{path}?{urllib.parse.urlencode(query)}"
    try:
        with urllib.request.urlopen(url) as res:
            return json.load(res)
    except urllib.error.HTTPError as e:
        sys.exit(f"GET {url}: {e.code} {e.read().decode()}")


def load_records():
    if not LOG.exists():
        return []
    with LOG.open(encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def attr(command, name):
    for a in command.get("attr", []):
        if a["name"] == name:
            value = a.get("value", {})
            return next((v for k, v in value.items() if k != "type"), None)
    return None


def mode_samples(args, window):
    """運用モードの値を、古い順にすべて (1000件ずつ取りに行く)"""
    query = {**window, "order": "asc", "limit": 1000}
    while True:
        page = request(args, "parameters/M5Sat/Bus/Mode", **query)
        yield from page.get("parameter", [])
        if not page.get("continuationToken"):
            return
        query["next"] = page["continuationToken"]


def collect(args, start, stop):
    """(時刻, 種類, 内容) の並びを作る"""
    rows = []
    window = {"start": iso(start), "stop": iso(stop)}

    for alarm in request(args, "alarms", **window, limit=200).get("alarms", []):
        name = alarm["id"]["name"]
        value = alarm.get("parameterDetail", {}).get("triggerValue", {}).get("engValue", {})
        shown = next((v for k, v in value.items() if k not in ("type", "sint64Value")), "")
        rows.append((parse_time(alarm["triggerTime"]), "アラーム", f"{alarm['severity']} {name} = {shown}"))
        ack = alarm.get("acknowledgeInfo")
        if ack:
            message = f" 「{ack['acknowledgeMessage']}」" if ack.get("acknowledgeMessage") else ""
            rows.append((parse_time(ack["acknowledgeTime"]), "アラームの確認",
                         f"{name} ({ack.get('acknowledgedBy', '?')}){message}"))

    for event in request(args, "events", **window, order="asc", limit=500).get("events", []):
        rows.append((parse_time(event["generationTime"]), "イベント",
                     f"{event.get('severity', '')} {event.get('type', '')}: {event.get('message', '')}"))

    for command in request(args, "commands", **window, order="asc", limit=200).get("commands", []):
        status = attr(command, "CommandComplete_Status")
        message = attr(command, "CommandComplete_Message")
        if status is None:
            # 衛星から ACK が返らず、完了の記録が無い (Accepted の時間切れなど)
            accepted = attr(command, "Verifier_Accepted_Status")
            status, message = ("NOK", f"Accepted {accepted}") if accepted else ("?", None)
        user = attr(command, "username") or "?"
        result = f"{status} ({message})" if message else status
        rows.append((parse_time(command["generationTime"]), "コマンド",
                     f"{command['commandName'].rsplit('/', 1)[-1]} by {user} → {result}"))

    previous = None
    for sample in mode_samples(args, window):
        mode = sample["engValue"].get("stringValue")
        if mode != previous:
            rows.append((parse_time(sample["generationTime"]), "運用モード", mode))
            previous = mode

    rows.sort(key=lambda r: r[0])
    return rows


def elapsed(t0, t):
    seconds = int((t - t0).total_seconds())
    sign = "-" if seconds < 0 else "+"
    seconds = abs(seconds)
    return f"{sign}{seconds // 60:02d}:{seconds % 60:02d}"


def recovery(kind, t0, rows):
    """出題ごとの「復旧した」とみなす時刻と、その説明"""
    if kind == "gnss-stop":
        # 異常が続いているうちに戻すと、また SAFE に落ちる。最後に SAFE から戻った時刻を復旧とする
        recovered = None
        went_safe = False
        for t, what, text in rows:
            if what != "運用モード" or t < t0:
                continue
            if text == "SAFE":
                went_safe = True
                recovered = None
            elif text == "NOMINAL" and went_safe and recovered is None:
                recovered = t
        if recovered:
            return recovered, "最後に SAFE から NOMINAL に戻った"
        return None, "NOMINAL に戻っていない"
    if kind == "command-loss":
        for t, what, text in rows:
            if what == "コマンド" and t >= t0 and "→ OK" in text:
                return t, "コマンドが再び完了した"
        return None, "コマンドがまだ完了していない"
    if kind == "reboot":
        booted = None
        for t, what, text in rows:
            if what == "イベント" and "BOOT" in text and t >= t0:
                booted = t
            elif booted and what == "コマンド" and "→ OK" in text:
                return t, "再起動のあと、コマンドで状態を戻した (" + text.split(" by ")[0] + ")"
        return None, "再起動のあとのコマンドがまだない"
    return None, ""


def report(args, record, next_record=None):
    t0 = parse_time(record["time"])
    stop = t0 + datetime.timedelta(minutes=args.minutes)
    now = datetime.datetime.now(datetime.timezone.utc)
    stop = min(stop, now)
    if next_record:
        stop = min(stop, parse_time(next_record["time"]))
    rows = collect(args, t0 - datetime.timedelta(seconds=30), stop)

    kind = record["kind"]
    detail = f" {record['duration']}秒" if "duration" in record else ""
    print(f"出題: {kind}{detail}  {iso(t0)}" + (f"  ({record['note']})" if record.get("note") else ""))
    print(f"範囲: 出題の30秒前 〜 {args.minutes:g}分後 (次の出題か、今まで)")
    print()
    print(f"{'経過':>6}  {'時刻 (UTC)':<12}  {'種類':<10}  内容")
    for t, what, text in sorted(rows + [(t0, "出題", f"{kind}{detail}")], key=lambda r: r[0]):
        print(f"{elapsed(t0, t):>6}  {t:%H:%M:%S}      {what:<10}  {text}")

    print()
    # 運用者の最初の対応: アラームの確認か、運用者が送ったコマンド (Timeline の自動の手順 "System" は除く)
    actions = [(t, what, text) for t, what, text in rows
               if t >= t0 and (what == "アラームの確認" or (what == "コマンド" and " by System " not in text))]
    if actions:
        t, what, text = actions[0]
        print(f"気づくまで: {elapsed(t0, t)}  (運用者の最初の対応: {what} {text.split(' →')[0]})")
    else:
        print("気づくまで: 運用者の対応なし")
    t, why = recovery(kind, t0, rows)
    print(f"復旧まで:   {elapsed(t0, t) + '  (' + why + ')' if t else why}")
    if kind == "gnss-stop":
        safe_count = sum(1 for _, what, text in rows if what == "運用モード" and text == "SAFE")
        if safe_count > 1:
            print(f"注意: SAFE に {safe_count} 回落ちた (異常が続いているうちに戻した)")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true", help="これまでの出題の一覧")
    parser.add_argument("--index", type=int, help="振り返る出題の番号 (--list の番号。default: 最後)")
    parser.add_argument("--start", help="記録 (logs/training.jsonl) の無い出題を、この時刻から振り返る (UTC)")
    parser.add_argument("--kind", default="gnss-stop", choices=["gnss-stop", "command-loss", "reboot", "clear"],
                        help="--start のときの出題の種類")
    parser.add_argument("--minutes", type=float, default=15, help="出題から何分後までを見るか (default: 15)")
    parser.add_argument("--url", default="http://localhost:8090", help="YAMCSのURL")
    parser.add_argument("--instance", default="gnss")
    args = parser.parse_args()

    if args.start:
        report(args, {"time": args.start, "kind": args.kind})
        return
    records = load_records()
    if args.list:
        for i, r in enumerate(records, 1):
            detail = f" {r['duration']}秒" if "duration" in r else ""
            print(f"{i:>3}  {r['time']}  {r['kind']}{detail}  {r.get('note', '')}")
        if not records:
            print("出題の記録はありません (tools/inject_fault.py で出題すると残る)")
        return
    if not records:
        sys.exit("出題の記録がありません。tools/inject_fault.py で出題するか、--start で時刻を指定してください")
    index = args.index if args.index else len(records)
    if not 1 <= index <= len(records):
        sys.exit(f"--index は 1〜{len(records)}")
    report(args, records[index - 1], records[index] if index < len(records) else None)


if __name__ == "__main__":
    main()
