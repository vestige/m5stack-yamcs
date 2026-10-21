#!/usr/bin/env python3
"""異常対応の訓練 (5-10) で、出題者が衛星 M5Sat に訓練用の異常を起こす。

使い方:
    python3 tools/inject_fault.py gnss-stop --duration 120    # 120秒間、GPS のデータが届かなくなる
    python3 tools/inject_fault.py command-loss --duration 60  # 60秒間、コマンドを受け付けなくなる
    python3 tools/inject_fault.py reboot                      # 予告なく再起動する (ウォッチドッグ)
    python3 tools/inject_fault.py clear                       # 続いている異常をすべて止める
    python3 tools/inject_fault.py gnss-stop --duration 120 --delay 120  # 2分後に起こす

YAMCS を通さず、地上局 (tools/udp_receiver.py) へ直接送る。運用者の Command history や Events には出ない。
本物の訓練では、異常は地上のシミュレータに入れる。M5Sat では衛星 (M5Stack) がシミュレータの役も兼ねる。

起こした時刻と種類は logs/training.jsonl に残す。振り返りは tools/training_report.py で行う。
運用者には、このスクリプトの出力と logs/training.jsonl を見せない。
"""

import argparse
import datetime
import json
import pathlib
import socket
import struct
import time

APID_TRAINING = 111
KINDS = {"clear": 0, "gnss-stop": 1, "command-loss": 2, "reboot": 3}
LOG = pathlib.Path(__file__).resolve().parent.parent / "logs" / "training.jsonl"


def packet(kind, duration, sequence):
    """lib/Satellite/src/TrainingFault.h と同じ形"""
    payload = struct.pack(">BH", KINDS[kind], duration)
    header = struct.pack(">HHH", 0x1000 | APID_TRAINING, 0xC000 | (sequence & 0x3FFF), len(payload) - 1)
    return header + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kind", choices=list(KINDS))
    parser.add_argument("--duration", type=int, default=60, help="異常が続く秒数 (gnss-stop, command-loss。default: 60)")
    parser.add_argument("--delay", type=float, default=0, help="この秒数だけ待ってから起こす (default: 0)")
    parser.add_argument("--uplink", default="127.0.0.1:10025", help="地上局 (udp_receiver.py) の TC の受け口")
    parser.add_argument("--note", default="", help="記録に残すメモ (例: 運用者の名前)")
    args = parser.parse_args()
    if not 0 <= args.duration <= 0xFFFF:
        parser.error("--duration は 0〜65535 秒")

    if args.delay > 0:
        print(f"{args.delay:.0f}秒後に {args.kind} を起こします (Ctrl-C でやめる)")
        time.sleep(args.delay)

    timed = args.kind in ("gnss-stop", "command-loss")
    host, _, port = args.uplink.rpartition(":")
    sequence = int(time.time()) & 0x3FFF
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.sendto(packet(args.kind, args.duration if timed else 0, sequence), (host or "127.0.0.1", int(port)))

    now = datetime.datetime.now(datetime.timezone.utc)
    record = {"time": now.isoformat(timespec="milliseconds").replace("+00:00", "Z"), "kind": args.kind}
    if timed:
        record["duration"] = args.duration
    if args.note:
        record["note"] = args.note
    LOG.parent.mkdir(exist_ok=True)
    with LOG.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")

    detail = f" ({args.duration}秒)" if "duration" in record else ""
    print(f"{record['time']}  {args.kind}{detail} を送りました")
    print("地上局のログ (logs/udp_receiver.log) に [uplink] ... TRAINING と出ていれば、衛星へ中継されています")


if __name__ == "__main__":
    main()
