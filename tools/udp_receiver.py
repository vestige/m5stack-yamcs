#!/usr/bin/env python3
"""M5Stackから届くGNSSテレメトリ(CCSDS Space Packet)をUDPで受信して表示する。

使い方:
    python3 tools/udp_receiver.py            # 0.0.0.0:10015 で待ち受け
    python3 tools/udp_receiver.py --port 10015 --raw

パケット形式は lib/TelemetryPacket/src/TelemetryPacket.h を参照。
"""

import argparse
import datetime
import socket
import struct

PRIMARY_HEADER = struct.Struct(">HHH")
# time, ms, lat, lon, alt, sats, fixQuality, fixType, flags, uptime, ok, err
PAYLOAD = struct.Struct(">IHddfBBBBIII")
PACKET_SIZE = PRIMARY_HEADER.size + PAYLOAD.size

FLAG_TIME_VALID = 1 << 0
FLAG_DATE_VALID = 1 << 1
FLAG_POSITION_VALID = 1 << 2
FLAG_ALTITUDE_VALID = 1 << 3
FLAG_RMC_ACTIVE = 1 << 4

FIX_TYPE = {1: "NoFix", 2: "2D", 3: "3D"}
FIX_QUALITY = {0: "NoFix", 1: "GPS", 2: "DGPS", 3: "PPS", 4: "RTK", 5: "FloatRTK", 6: "Estimated"}


def decode(data):
    if len(data) < PACKET_SIZE:
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

    (utc, ms, lat, lon, alt, sats, quality, fix_type, flags, uptime, ok, err) = PAYLOAD.unpack_from(
        data, PRIMARY_HEADER.size
    )
    return header, {
        "utc": utc,
        "ms": ms,
        "lat": lat,
        "lon": lon,
        "alt": alt,
        "sats": sats,
        "quality": quality,
        "fix_type": fix_type,
        "flags": flags,
        "uptime": uptime,
        "ok": ok,
        "err": err,
    }


def format_packet(header, t):
    if t["utc"]:
        time = datetime.datetime.fromtimestamp(t["utc"], datetime.timezone.utc)
        time_text = time.strftime("%Y-%m-%dT%H:%M:%S") + f".{t['ms']:03d}Z"
    else:
        time_text = "----------T--:--:--.---Z"

    flags = t["flags"]
    pos = f"lat={t['lat']:.6f} lon={t['lon']:.6f}" if flags & FLAG_POSITION_VALID else "lat=--- lon=---"
    alt = f"alt={t['alt']:.1f}m" if flags & FLAG_ALTITUDE_VALID else "alt=---"
    quality = FIX_QUALITY.get(t["quality"], "Unknown")
    rmc = "A" if flags & FLAG_RMC_ACTIVE else "V"

    return (
        f"apid={header['apid']} seq={header['seq']:5d} {time_text} {pos} {alt} "
        f"sats={t['sats']} fix={quality}({t['quality']}) type={FIX_TYPE.get(t['fix_type'], '?')} rmc={rmc} "
        f"uptime={t['uptime'] / 1000:.1f}s ok={t['ok']} err={t['err']}"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="0.0.0.0", help="待ち受けるアドレス (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=10015, help="待ち受けるUDPポート (default: 10015)")
    parser.add_argument("--raw", action="store_true", help="受信したバイト列も16進で表示する")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    print(f"Listening on udp://{args.host}:{args.port} (Ctrl+C to stop)")

    last_seq = None
    try:
        while True:
            data, (addr, _) = sock.recvfrom(2048)
            if args.raw:
                print(f"[{addr}] {data.hex(' ')}")
            try:
                header, telemetry = decode(data)
            except ValueError as e:
                print(f"[{addr}] invalid packet: {e}")
                continue

            # シーケンス番号の飛びからパケットロスを検出する(14bitで一周する)
            if last_seq is not None:
                lost = (header["seq"] - last_seq - 1) & 0x3FFF
                if lost:
                    print(f"[{addr}] {lost} packet(s) lost")
            last_seq = header["seq"]

            print(f"[{addr}] {format_packet(header, telemetry)}")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
