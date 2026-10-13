#!/usr/bin/env python3
"""YAMCSのAPIで衛星M5Satにコマンドを送り、Command Verification の結果を表示する。

使い方:
    python3 tools/send_command.py NO_OP
    python3 tools/send_command.py BEEP Duration=300
    python3 tools/send_command.py SET_MODE Mode=SAFE

Web UI の Commanding → Send a command と同じことをする。
"""

import argparse
import json
import time
import urllib.parse
import urllib.request

STAGES = ["Acknowledge_Sent_Status", "Verifier_Accepted_Status", "Verifier_Complete_Status",
          "Verifier_Failed_Status", "CommandComplete_Status"]


def request(url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as res:
        return json.load(res)


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
    args = parser.parse_args()

    command_args = dict(a.split("=", 1) for a in args.args)
    command_args = {k: parse_value(v) for k, v in command_args.items()}
    name = args.command if args.command.startswith("/") else f"/M5Sat/Bus/{args.command}"

    sent = request(f"{args.url}/api/processors/{args.instance}/realtime/commands{name}", {"args": command_args})
    command_id = sent["id"]
    print(f"sent {name} {command_args or ''}")

    time.sleep(args.wait)
    entry = request(f"{args.url}/api/archive/{args.instance}/commands/{urllib.parse.quote(command_id, safe='')}")
    # 値は {"type": "STRING", "stringValue": "OK"} の形で返ってくる
    attrs = {a["name"]: next((v for k, v in a["value"].items() if k != "type"), None)
             for a in entry.get("attr", []) if a.get("value")}
    for stage in STAGES:
        if stage in attrs:
            print(f"  {stage.replace('_Status', ''):<20} {attrs[stage]}")


if __name__ == "__main__":
    main()
