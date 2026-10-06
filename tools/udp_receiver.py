#!/usr/bin/env python3
"""M5Stack (衛星M5Sat) から届くテレメトリ(CCSDS Space Packet)をUDPで受信して表示する。

使い方:
    python3 tools/udp_receiver.py            # 0.0.0.0:10015 で待ち受け
    python3 tools/udp_receiver.py --port 10015 --raw
    python3 tools/udp_receiver.py --forward 127.0.0.1:10016   # YAMCSへ中継する

パケット形式:
    APID 100 GNSSテレメトリ  lib/TelemetryPacket/src/TelemetryPacket.h
    APID 101 HK             lib/TelemetryPacket/src/HousekeepingPacket.h
"""

import argparse
import datetime
import socket
import struct

PRIMARY_HEADER = struct.Struct(">HHH")

APID_GNSS = 100
APID_HOUSEKEEPING = 101

# time, ms, lat, lon, alt, sats, fixQuality, fixType, flags, uptime, ok, err
GNSS_PAYLOAD = struct.Struct(">IHddfBBBBIII")
# mode, payloadPower, accepted, rejected, lastCommand, rssi, freeHeap, minFreeHeap, uptime, bootCount, resetReason
HOUSEKEEPING_PAYLOAD = struct.Struct(">BBHHBbIIIHB")

FLAG_TIME_VALID = 1 << 0
FLAG_DATE_VALID = 1 << 1
FLAG_POSITION_VALID = 1 << 2
FLAG_ALTITUDE_VALID = 1 << 3
FLAG_RMC_ACTIVE = 1 << 4

FIX_TYPE = {1: "NoFix", 2: "2D", 3: "3D"}
FIX_QUALITY = {0: "NoFix", 1: "GPS", 2: "DGPS", 3: "PPS", 4: "RTK", 5: "FloatRTK", 6: "Estimated"}
MODE = {0: "SAFE", 1: "NOMINAL", 2: "MISSION"}
# ESP-IDF の esp_reset_reason_t
RESET_REASON = {
    0: "UNKNOWN",
    1: "POWER_ON",
    2: "EXTERNAL",
    3: "SOFTWARE",
    4: "PANIC",
    5: "INTERRUPT_WATCHDOG",
    6: "TASK_WATCHDOG",
    7: "OTHER_WATCHDOG",
    8: "DEEP_SLEEP",
    9: "BROWNOUT",
    10: "SDIO",
}


def decode_header(data):
    if len(data) < PRIMARY_HEADER.size:
        raise ValueError(f"packet too short: {len(data)} bytes")

    word1, word2, length = PRIMARY_HEADER.unpack_from(data)
    header = {
        "version": word1 >> 13,
        "type": (word1 >> 12) & 1,
        "apid": word1 & 0x07FF,
        "seq": word2 & 0x3FFF,
        "length": length,
    }
    if length + 1 + PRIMARY_HEADER.size != len(data):
        raise ValueError(f"length field {length} does not match packet size {len(data)}")
    return header


def unpack_payload(payload_struct, data):
    if len(data) != PRIMARY_HEADER.size + payload_struct.size:
        raise ValueError(f"unexpected packet size: {len(data)} bytes")
    return payload_struct.unpack_from(data, PRIMARY_HEADER.size)


def format_gnss(data):
    (utc, ms, lat, lon, alt, sats, quality, fix_type, flags, uptime, ok, err) = unpack_payload(
        GNSS_PAYLOAD, data
    )
    if utc:
        time = datetime.datetime.fromtimestamp(utc, datetime.timezone.utc)
        time_text = time.strftime("%Y-%m-%dT%H:%M:%S") + f".{ms:03d}Z"
    else:
        time_text = "----------T--:--:--.---Z"

    pos = f"lat={lat:.6f} lon={lon:.6f}" if flags & FLAG_POSITION_VALID else "lat=--- lon=---"
    alt_text = f"alt={alt:.1f}m" if flags & FLAG_ALTITUDE_VALID else "alt=---"
    rmc = "A" if flags & FLAG_RMC_ACTIVE else "V"
    return (
        f"GNSS {time_text} {pos} {alt_text} "
        f"sats={sats} fix={FIX_QUALITY.get(quality, 'Unknown')}({quality}) type={FIX_TYPE.get(fix_type, '?')} "
        f"rmc={rmc} uptime={uptime / 1000:.1f}s ok={ok} err={err}"
    )


def format_housekeeping(data):
    (mode, payload, accepted, rejected, last_command, rssi, free_heap, min_free_heap, uptime, boot_count,
     reset_reason) = unpack_payload(HOUSEKEEPING_PAYLOAD, data)
    return (
        f"HK   mode={MODE.get(mode, mode)} payload={'ON' if payload else 'OFF'} "
        f"cmd(ok={accepted} ng={rejected} last={last_command}) rssi={rssi}dBm "
        f"heap={free_heap}(min {min_free_heap}) uptime={uptime / 1000:.1f}s "
        f"boot={boot_count} reset={RESET_REASON.get(reset_reason, reset_reason)}"
    )


FORMATTERS = {APID_GNSS: format_gnss, APID_HOUSEKEEPING: format_housekeeping}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="0.0.0.0", help="待ち受けるアドレス (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=10015, help="待ち受けるUDPポート (default: 10015)")
    parser.add_argument("--raw", action="store_true", help="受信したバイト列も16進で表示する")
    parser.add_argument("--forward", metavar="HOST:PORT", help="受信したパケットをそのまま転送する先 (例: YAMCS)")
    args = parser.parse_args()

    forward_to = None
    if args.forward:
        host, _, port = args.forward.rpartition(":")
        forward_to = (host or "127.0.0.1", int(port))

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    print(f"Listening on udp://{args.host}:{args.port} (Ctrl+C to stop)")
    forward_sock = None
    if forward_to:
        forward_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(f"Forwarding to udp://{forward_to[0]}:{forward_to[1]}")

    # シーケンス番号はAPIDごとに数えられている
    last_seq = {}
    try:
        while True:
            data, (addr, _) = sock.recvfrom(2048)
            if forward_sock:
                # 中身の検査は転送先(YAMCS)に任せ、届いたものはすべて転送する。
                # 転送先が止まっていても受信は続ける
                try:
                    forward_sock.sendto(data, forward_to)
                except OSError:
                    pass
            if args.raw:
                print(f"[{addr}] {data.hex(' ')}")

            try:
                header = decode_header(data)
                formatter = FORMATTERS.get(header["apid"])
                if formatter is None:
                    raise ValueError(f"unknown APID {header['apid']}")
                text = formatter(data)
            except ValueError as e:
                print(f"[{addr}] invalid packet: {e}")
                continue

            # シーケンス番号の飛びからパケットロスを検出する(14bitで一周する)
            apid = header["apid"]
            if apid in last_seq:
                lost = (header["seq"] - last_seq[apid] - 1) & 0x3FFF
                if lost:
                    print(f"[{addr}] apid={apid}: {lost} packet(s) lost")
            last_seq[apid] = header["seq"]

            print(f"[{addr}] apid={apid} seq={header['seq']:5d} {text}")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
