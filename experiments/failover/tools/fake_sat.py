#!/usr/bin/env python3
"""冗長化の実験用の、模擬の衛星と地上局 (M5Stack がなくても動く)。

- 1秒ごとに HK テレメトリ (APID 101) を、いま「接続先」になっている YAMCS へ送る。
  接続先は target ファイル (primary / standby) で決まる。地上局がどちらの系とつながるかの
  切り替えにあたる。tools/failover.py が切替のときに書き換える。
- YAMCS からの TC (APID 110) を受け取り、どちらの系から届いたかを記録する。
  Active 系の TC は 10125、Standby 系の TC は 10126 に届くようにしてあるので、送り元がわかる。
- TC を受け取ったら ACK (APID 102) を、接続先の YAMCS へ返す (Command Verification が進む)。
- 二重送信を見張る: 接続先でない系から TC が届いた、または両方の系から WINDOW 秒以内に
  TC が届いたら、警告を出して events ファイルに残す。

使い方 (experiments/failover/start.sh が起動する):
    python3 tools/fake_sat.py --run-dir .run --log-dir logs
"""

import argparse
import json
import os
import select
import socket
import struct
import time

HK_APID = 101
ACK_APID = 102
TC_APID = 110

TM_PORTS = {"primary": 10116, "standby": 10216}
TC_PORTS = {"primary": 10125, "standby": 10126}
DOUBLE_WINDOW_SEC = 30.0


def now_ms():
    return int(time.time() * 1000)


def header(apid, seq, size, time_ms):
    # version=0, type=TM, secondary header flag=1, APID / unsegmented, seq / length / 機上時刻 (ms)
    return struct.pack(">HHHQ", 0x0800 | (apid & 0x07FF), 0xC000 | (seq & 0x3FFF), size - 6 - 1, time_ms)


class Satellite:
    def __init__(self):
        self.boot = time.time()
        self.seq = 0
        self.accepted = 0
        self.last_command = 0

    def next_seq(self):
        self.seq = (self.seq + 1) & 0x3FFF
        return self.seq

    def hk(self):
        """lib/TelemetryPacket/src/HousekeepingPacket.h と同じ並び (99 バイト)"""
        t = now_ms()
        body = struct.pack(
            ">BBHHBbIIIHBBBHIBIHBIHHBHHBHIIIBBIIII",
            1, 1,                      # mode=NOMINAL, payload power=ON
            self.accepted & 0xFFFF, 0,  # accepted / rejected
            self.last_command, -40,    # last command id, RSSI
            200000, 150000,            # free heap, min free heap
            int((time.time() - self.boot) * 1000) & 0xFFFFFFFF,  # uptime (ms)
            1, 1, 1, 0, 0,             # boot count, reset reason, FDIR, last fault, fault count
            int(time.time()),          # onboard time
            0, 0, 0, 0, 0, 0, 0,       # observation
            0, 0, 0,                   # lamp, payload on time
            0, 0, 0, 0, 0,             # recorder
            0, 0, 0, 0, 0, 0,          # dump, released
        )
        return header(HK_APID, self.next_seq(), 99, t) + body

    def ack(self, tc_seq, command_id, stage):
        """lib/TelemetryPacket/src/AckPacket.h と同じ並び (19 バイト)。stage: 1=受理, 3=完了"""
        return header(ACK_APID, self.next_seq(), 19, now_ms()) + struct.pack(">HBBB", tc_seq, command_id, stage, 0)


class Monitor:
    """どの系から TC が届いたかを覚えて、二重送信を見張る"""

    def __init__(self, events_path, status_path):
        self.events_path = events_path
        self.status_path = status_path
        self.counts = {"primary": 0, "standby": 0}
        self.last_tc = {"primary": None, "standby": None}
        self.alerts = 0

    def event(self, level, message, **extra):
        record = {"time": time.strftime("%Y-%m-%dT%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}",
                  "level": level, "message": message, **extra}
        print(f"[{record['time']}] {level:8s} {message}", flush=True)
        with open(self.events_path, "a") as f:
            f.write(json.dumps(record, ensure_ascii=False) + "\n")

    def on_tc(self, source, target, command_id, tc_seq):
        t = time.time()
        self.counts[source] += 1
        self.last_tc[source] = t
        self.event("INFO", f"TC を受信: {source} 系から (command id={command_id}, seq={tc_seq})",
                   source=source, commandId=command_id, seq=tc_seq)
        if source != target:
            self.alerts += 1
            self.event("CRITICAL", f"接続先でない {source} 系から TC が届いた (接続先は {target})",
                       source=source, target=target)
        other = "standby" if source == "primary" else "primary"
        if self.last_tc[other] is not None and t - self.last_tc[other] < DOUBLE_WINDOW_SEC:
            self.alerts += 1
            self.event("CRITICAL", f"二重送信の疑い: {DOUBLE_WINDOW_SEC:.0f} 秒以内に両方の系から TC が届いた")
        self.write_status(target)

    def on_switch(self):
        """接続先を切り替えたら、それまでに届いた TC の時刻を忘れる。
        切替の前に旧 Active 系から届いた TC は正しいもので、二重送信ではない。
        切替のあとに旧 Active 系から届いたものは、on_tc の「接続先でない系から」で見つかる"""
        self.last_tc = {"primary": None, "standby": None}

    def write_status(self, target):
        status = {"target": target, "tcCount": self.counts, "alerts": self.alerts, "updated": time.time()}
        tmp = self.status_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(status, f)
        os.replace(tmp, self.status_path)


def read_target(path):
    try:
        with open(path) as f:
            value = f.read().strip()
        return value if value in TM_PORTS else "primary"
    except FileNotFoundError:
        return "primary"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--run-dir", default=".run")
    parser.add_argument("--log-dir", default="logs")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--interval", type=float, default=1.0, help="HK を送る間隔 (秒)")
    args = parser.parse_args()
    os.makedirs(args.run_dir, exist_ok=True)
    os.makedirs(args.log_dir, exist_ok=True)

    target_path = os.path.join(args.run_dir, "target")
    if not os.path.exists(target_path):
        with open(target_path, "w") as f:
            f.write("primary\n")
    monitor = Monitor(os.path.join(args.log_dir, "fake_sat_events.jsonl"), os.path.join(args.run_dir, "fake_sat_status.json"))
    sat = Satellite()

    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tc_socks = {}
    for name, port in TC_PORTS.items():
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.bind((args.host, port))
        tc_socks[s] = name

    target = read_target(target_path)
    monitor.event("INFO", f"模擬衛星・地上局を開始 (接続先: {target} 系, TM udp:{TM_PORTS[target]})")
    monitor.write_status(target)
    next_hk = time.time()
    while True:
        timeout = max(0.0, next_hk - time.time())
        readable, _, _ = select.select(list(tc_socks), [], [], timeout)
        current = read_target(target_path)
        if current != target:
            monitor.event("INFO", f"接続先を切り替え: {target} 系 → {current} 系")
            target = current
            monitor.on_switch()
            monitor.write_status(target)
        for s in readable:
            data, _ = s.recvfrom(4096)
            source = tc_socks[s]
            if len(data) < 7:
                monitor.event("WARNING", f"{source} 系から短すぎる TC ({len(data)} バイト)")
                continue
            packet_id, seq_word = struct.unpack(">HH", data[:4])
            tc_seq = seq_word & 0x3FFF
            command_id = data[6]
            if packet_id & 0x07FF != TC_APID:
                monitor.event("WARNING", f"{source} 系から APID {packet_id & 0x07FF} の TC")
            monitor.on_tc(source, target, command_id, tc_seq)
            sat.accepted += 1
            sat.last_command = command_id
            # ACK は接続先の系にだけ返る (実際の地上局も、つながっている系にしか TM を流さない)
            for stage in (1, 3):
                out.sendto(sat.ack(tc_seq, command_id, stage), (args.host, TM_PORTS[target]))
        if time.time() >= next_hk:
            out.sendto(sat.hk(), (args.host, TM_PORTS[target]))
            next_hk += args.interval


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
