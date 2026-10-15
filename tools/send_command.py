#!/usr/bin/env python3
"""YAMCSのAPIで衛星M5Satにコマンドを送り、Command Verification の結果を表示する。

使い方:
    python3 tools/send_command.py NO_OP
    python3 tools/send_command.py BEEP Duration=300
    python3 tools/send_command.py SET_MODE Mode=SAFE             # 承認待ちの待ち行列に入る
    python3 tools/send_command.py SET_MODE Mode=SAFE --approve   # 試験用: 待ち行列で自動で承認する

Web UI の Commanding → Send a command と同じことをする。
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

STAGES = ["Acknowledge_Queued_Status", "TransmissionConstraints_Status", "Acknowledge_Released_Status",
          "Acknowledge_Sent_Status", "Verifier_Accepted_Status", "Verifier_Complete_Status",
          "Verifier_Failed_Status", "CommandComplete_Status"]


def request(url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as res:
        text = res.read()
        return json.loads(text) if text else {}


def approve(args, command_id):
    time.sleep(0.5)
    queues = request(f"{args.url}/api/processors/{args.instance}/realtime/queues")["queues"]
    for queue in queues:
        for entry in queue.get("entries", []):
            if entry["id"] == command_id:
                quoted = urllib.parse.quote(command_id, safe="")
                request(f"{args.url}/api/processors/{args.instance}/realtime/queues/{queue['name']}"
                        f"/commands/{quoted}:accept", {})
                print(f"  approved in queue '{queue['name']}'")


def parse_value(text):
    try:
        return int(text)
    except ValueError:
        return text


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", help="コマンド名 (例: BEEP)")
    parser.add_argument("args", nargs="*", metavar="NAME=VALUE", help="引数 (例: Duration=300)")
    parser.add_argument("--url", default="http://localhost:8090", help="YAMCSのURL")
    parser.add_argument("--instance", default="gnss")
    parser.add_argument("--wait", type=float, default=3.0, help="結果を待つ秒数 (default: 3)")
    parser.add_argument("--approve", action="store_true",
                        help="試験用: 承認待ちの待ち行列に入ったら自動で承認する (普段は Web UI の Queues で人が承認する)")
    args = parser.parse_args()

    command_args = dict(a.split("=", 1) for a in args.args)
    command_args = {k: parse_value(v) for k, v in command_args.items()}
    name = args.command if args.command.startswith("/") else f"/M5Sat/Bus/{args.command}"

    try:
        sent = request(f"{args.url}/api/processors/{args.instance}/realtime/commands{name}", {"args": command_args})
    except urllib.error.HTTPError as e:
        # 引数の範囲外などは、YAMCSが受け付けずに理由を返す
        print(f"rejected by YAMCS: {json.load(e).get('msg', e)}")
        sys.exit(1)
    command_id = sent["id"]
    print(f"sent {name} {command_args or ''}")
    if args.approve:
        approve(args, command_id)

    time.sleep(args.wait)
    entry = request(f"{args.url}/api/archive/{args.instance}/commands/{urllib.parse.quote(command_id, safe='')}")
    # 値は {"type": "STRING", "stringValue": "OK"} の形で返ってくる
    attrs = {a["name"]: next((v for k, v in a["value"].items() if k != "type"), None)
             for a in entry.get("attr", []) if a.get("value")}
    for stage in STAGES:
        if stage in attrs:
            message = attrs.get(stage.replace("_Status", "_Message"), "")
            print(f"  {stage.replace('_Status', ''):<32} {attrs[stage]} {message}")
    if "Acknowledge_Released_Status" not in attrs and attrs.get("Acknowledge_Queued_Status") == "OK":
        print("  -> 待ち行列で承認を待っています (Commanding → Queues)")


if __name__ == "__main__":
    main()
