#!/usr/bin/env python3
"""ミッション要求サービス (#44)。利用者の要求 (ランプの色と時間) を受け、検査してコマンドにし、YAMCS へ送る。

使い方:
    python3 tools/mission_api.py                 # http://127.0.0.1:8095 で待ち受ける (scripts/start.sh が起動する)

API:
    POST /requests   {"color": "CYAN", "duration": 60, "requester": "A社"}
                     → 検査して SET_LAMP のコマンドにし、YAMCS へ送る。コマンドは Queues で運用者の承認を待つ。
                       衛星が SAFE のとき、ペイロードの電源が切れているとき、テレメトリが届いていないときは断る
    GET  /requests   要求の一覧 (新しい順)。状態は YAMCS のコマンドの履歴から作る
    GET  /health     サービスが動いているか

本物の運用の「ミッション要求」にあたる。利用者は「何をしたいか」だけを出し、
コマンドにするのも、送ってよいかを判断する (承認する) のも、運用の側が受け持つ。
要求の記録は logs/mission_requests.jsonl に残す。
"""

import argparse
import datetime
import json
import pathlib
import threading
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = pathlib.Path(__file__).resolve().parent.parent / "logs" / "mission_requests.jsonl"
COLORS = ["RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA", "WHITE"]
MIN_DURATION = 1
MAX_DURATION = 600
COMMAND = "/M5Sat/Bus/SET_LAMP"

lock = threading.Lock()


class Rejected(Exception):
    """要求を受け付けない理由"""


def now_iso():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def yamcs(args, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"{args.yamcs}/api{path}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as res:
        text = res.read()
        return json.loads(text) if text else {}


def load_requests():
    if not LOG.exists():
        return []
    with LOG.open(encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


# --- 要求の検査 -----------------------------------------------------------------

def validate(body):
    """利用者の言葉 (色、時間、名前) を検査する。YAMCS の検査より前に、利用者にわかる言葉で断る"""
    color = str(body.get("color", "")).strip().upper()
    if color not in COLORS:
        raise Rejected(f"色は {', '.join(COLORS)} から選んでください")
    try:
        duration = int(body.get("duration"))
    except (TypeError, ValueError):
        raise Rejected("点灯時間 (秒) を数で入れてください")
    if not MIN_DURATION <= duration <= MAX_DURATION:
        raise Rejected(f"点灯時間は {MIN_DURATION}〜{MAX_DURATION} 秒にしてください")
    requester = str(body.get("requester", "")).strip()
    if not requester or len(requester) > 32:
        raise Rejected("要求する人 (会社や名前) を32文字以内で入れてください")
    return color, duration, requester


def check_satellite(args):
    """衛星の今の状態で、要求を受けられるかを確かめる"""
    try:
        mode = yamcs(args, f"/processors/{args.instance}/realtime/parameters/M5Sat/Bus/Mode")
    except urllib.error.URLError:
        raise Rejected("管制局 (YAMCS) につながりません")
    if mode.get("acquisitionStatus") != "ACQUIRED":
        raise Rejected("いまは衛星と通信できません (テレメトリが届いていない)。次のパスで要求してください")
    if mode.get("engValue", {}).get("stringValue") == "SAFE":
        raise Rejected("衛星は SAFE (異常からの退避中) なので、ミッション要求は受けられません")
    power = yamcs(args, f"/processors/{args.instance}/realtime/parameters/M5Sat/Bus/PayloadPower")
    if not power.get("engValue", {}).get("booleanValue"):
        raise Rejected("いまはペイロード (ランプ) の電源が切れているので、要求は受けられません")


def submit(args, body):
    color, duration, requester = validate(body)
    check_satellite(args)
    with lock:
        number = len(load_requests()) + 1
        comment = f"ミッション要求 #{number} ({requester})"
        try:
            issued = yamcs(args, f"/processors/{args.instance}/realtime/commands{COMMAND}",
                           {"args": {"Color": color, "Duration": duration}, "comment": comment})
        except urllib.error.HTTPError as e:
            detail = json.loads(e.read() or b"{}").get("msg", str(e))
            raise Rejected(f"管制局 (YAMCS) がコマンドを受け付けませんでした: {detail}")
        record = {"id": number, "time": now_iso(), "requester": requester, "color": color, "duration": duration,
                  "commandId": issued.get("id")}
        LOG.parent.mkdir(exist_ok=True)
        with LOG.open("a", encoding="utf-8") as f:
            f.write(json.dumps(record, ensure_ascii=False) + "\n")
    return record


# --- 要求の状態 -----------------------------------------------------------------

def attr(command, name):
    for a in command.get("attr", []):
        if a["name"] == name:
            return next((v for k, v in a.get("value", {}).items() if k != "type"), None)
    return None


def status_of(args, record):
    """YAMCS のコマンドの履歴から、要求の状態を作る"""
    command_id = record.get("commandId")
    if not command_id:
        return "不明", ""
    quoted = urllib.parse.quote(command_id, safe="")
    try:
        command = yamcs(args, f"/archive/{args.instance}/commands/{quoted}")
    except urllib.error.HTTPError:
        return "承認待ち", "Queues で運用者の承認を待っています"
    except urllib.error.URLError:
        return "不明", "管制局 (YAMCS) につながりません"
    complete = attr(command, "CommandComplete_Status")
    message = attr(command, "CommandComplete_Message") or ""
    if complete == "OK":
        return "実行完了", "衛星がランプを点けました"
    if complete == "NOK":
        if attr(command, "Acknowledge_Released_Status") is None:
            return "却下", message or "運用者が承認しませんでした"
        return "失敗", message
    if attr(command, "Acknowledge_Sent_Status") == "OK":
        return "送信済み", "衛星の応答を待っています"
    if attr(command, "Acknowledge_Released_Status") == "OK":
        return "承認済み", "衛星へ送っています"
    return "承認待ち", "Queues で運用者の承認を待っています"


def list_requests(args):
    rows = []
    for record in reversed(load_requests()[-50:]):
        status, message = status_of(args, record)
        rows.append({**record, "status": status, "message": message})
    return rows


# --- HTTP -----------------------------------------------------------------------

def make_handler(args):
    class Handler(BaseHTTPRequestHandler):
        def send_json(self, code, body):
            data = json.dumps(body, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            # Grafana のフォーム (ブラウザ) から呼ぶので、Grafana の画面からの呼び出しを許す
            self.send_header("Access-Control-Allow-Origin", args.allow_origin)
            self.end_headers()
            self.wfile.write(data)

        def do_OPTIONS(self):
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", args.allow_origin)
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()

        def do_GET(self):
            path = urllib.parse.urlparse(self.path).path
            if path == "/health":
                self.send_json(200, {"status": "ok"})
            elif path == "/requests":
                self.send_json(200, list_requests(args))
            else:
                self.send_json(404, {"error": "not found"})

        def do_POST(self):
            if urllib.parse.urlparse(self.path).path != "/requests":
                self.send_json(404, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", 0))
                body = json.loads(self.rfile.read(length) or b"{}")
                record = submit(args, body)
            except json.JSONDecodeError:
                self.send_json(400, {"accepted": False, "message": "要求の形 (JSON) が正しくありません"})
                return
            except Rejected as e:
                self.send_json(400, {"accepted": False, "message": str(e)})
                return
            self.send_json(200, {"accepted": True, "id": record["id"],
                                 "message": f"要求 #{record['id']} を受け付けました。運用者の承認を待っています",
                                 **record})

        def log_message(self, fmt, *a):
            print(f"{now_iso()} {self.address_string()} {fmt % a}", flush=True)

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8095)
    parser.add_argument("--yamcs", default="http://localhost:8090", help="YAMCSのURL")
    parser.add_argument("--instance", default="gnss")
    parser.add_argument("--allow-origin", default="http://localhost:3000", help="呼び出しを許す画面 (Grafana)")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(args))
    print(f"mission_api: listening on http://127.0.0.1:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
