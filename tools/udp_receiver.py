#!/usr/bin/env python3
"""M5Stack (衛星M5Sat) から届くテレメトリ(CCSDS Space Packet)をUDPで受信して表示する。

使い方:
    python3 tools/udp_receiver.py            # 0.0.0.0:10015 で待ち受け
    python3 tools/udp_receiver.py --port 10015 --raw
    python3 tools/udp_receiver.py --forward 127.0.0.1:10016   # YAMCSへ中継する
    python3 tools/udp_receiver.py --forward 127.0.0.1:10016 --uplink-port 10025
        # さらに、YAMCSから 127.0.0.1:10025 に届いたTCを衛星へ中継する

TCは、直近にTMを送ってきた衛星のIPアドレスへ送る。TMが届いていない (衛星が見えていない) 間は送れない。

パケット形式:
    APID 100 GNSSテレメトリ  lib/TelemetryPacket/src/TelemetryPacket.h
    APID 101 HK             lib/TelemetryPacket/src/HousekeepingPacket.h
    APID 102 ACK            lib/TelemetryPacket/src/AckPacket.h
    APID 110 TC             lib/Satellite/src/Telecommand.h
"""

import argparse
import datetime
import select
import socket
import struct

PRIMARY_HEADER = struct.Struct(">HHH")

APID_GNSS = 100
APID_HOUSEKEEPING = 101
APID_ACK = 102
APID_TELECOMMAND = 110

# time, ms, lat, lon, alt, sats, fixQuality, fixType, flags, uptime, ok, err
GNSS_PAYLOAD = struct.Struct(">IHddfBBBBIII")
# mode, payloadPower, accepted, rejected, lastCommand, rssi, freeHeap, minFreeHeap, uptime, bootCount, resetReason
HOUSEKEEPING_PAYLOAD = struct.Struct(">BBHHBbIIIHB")
# tcSequence, commandId, stage, errorCode
ACK_PAYLOAD = struct.Struct(">HBBB")

FLAG_TIME_VALID = 1 << 0
FLAG_DATE_VALID = 1 << 1
FLAG_POSITION_VALID = 1 << 2
FLAG_ALTITUDE_VALID = 1 << 3
FLAG_RMC_ACTIVE = 1 << 4

FIX_TYPE = {1: "NoFix", 2: "2D", 3: "3D"}
FIX_QUALITY = {0: "NoFix", 1: "GPS", 2: "DGPS", 3: "PPS", 4: "RTK", 5: "FloatRTK", 6: "Estimated"}
MODE = {0: "SAFE", 1: "NOMINAL", 2: "MISSION"}
COMMAND = {1: "NO_OP", 2: "SET_MODE", 3: "SET_TM_INTERVAL", 4: "PAYLOAD_POWER", 5: "RESET_COUNTERS", 6: "BEEP"}
ACK_STAGE = {1: "ACCEPTED", 2: "REJECTED", 3: "COMPLETED", 4: "FAILED"}
ERROR_CODE = {
    0: "NONE",
    1: "UNKNOWN_COMMAND",
    2: "INVALID_LENGTH",
    3: "INVALID_ARGUMENT",
    4: "NOT_ALLOWED",
    5: "MALFORMED_PACKET",
}
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
        f"cmd(ok={accepted} ng={rejected} last={COMMAND.get(last_command, 'NONE')}) rssi={rssi}dBm "
        f"heap={free_heap}(min {min_free_heap}) uptime={uptime / 1000:.1f}s "
        f"boot={boot_count} reset={RESET_REASON.get(reset_reason, reset_reason)}"
    )


def format_ack(data):
    tc_seq, command_id, stage, error = unpack_payload(ACK_PAYLOAD, data)
    text = f"ACK  tc_seq={tc_seq} {COMMAND.get(command_id, command_id)} {ACK_STAGE.get(stage, stage)}"
    if error:
        text += f" error={ERROR_CODE.get(error, error)}"
    return text


def format_telecommand(data):
    header = decode_header(data)
    command_id = data[PRIMARY_HEADER.size] if len(data) > PRIMARY_HEADER.size else None
    args = data[PRIMARY_HEADER.size + 1:]
    return f"TC   seq={header['seq']} {COMMAND.get(command_id, command_id)} args={args.hex() or '-'}"


FORMATTERS = {APID_GNSS: format_gnss, APID_HOUSEKEEPING: format_housekeeping, APID_ACK: format_ack}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="0.0.0.0", help="待ち受けるアドレス (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=10015, help="待ち受けるUDPポート (default: 10015)")
    parser.add_argument("--raw", action="store_true", help="受信したバイト列も16進で表示する")
    parser.add_argument("--forward", metavar="HOST:PORT", help="受信したパケットをそのまま転送する先 (例: YAMCS)")
    parser.add_argument("--uplink-port", type=int, help="YAMCSからTCを受け取る 127.0.0.1 のUDPポート (例: 10025)")
    parser.add_argument("--satellite-tc-port", type=int, default=10025, help="衛星がTCを待ち受けるポート (default: 10025)")
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
    uplink_sock = None
    if args.uplink_port:
        # YAMCSからは 127.0.0.1 で受け取り、衛星へは別のソケットで送る
        # (127.0.0.1 に結び付けたソケットからはLAN上の相手に送れない)
        uplink_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        uplink_sock.bind(("127.0.0.1", args.uplink_port))
        satellite_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(f"Uplinking TC from udp://127.0.0.1:{args.uplink_port} to the satellite port {args.satellite_tc_port}")

    # シーケンス番号はAPIDごとに数えられている
    last_seq = {}
    satellite_addr = None
    try:
        while True:
            readable, _, _ = select.select([s for s in (sock, uplink_sock) if s], [], [])
            if uplink_sock in readable:
                tc, _ = uplink_sock.recvfrom(2048)
                if satellite_addr is None:
                    print(f"[uplink] satellite not in view, dropped: {tc.hex(' ')}")
                else:
                    try:
                        text = format_telecommand(tc)
                    except ValueError as e:
                        text = f"TC   (invalid: {e})"
                    try:
                        satellite_sock.sendto(tc, (satellite_addr, args.satellite_tc_port))
                        print(f"[uplink -> {satellite_addr}] {text}")
                    except OSError as e:
                        print(f"[uplink -> {satellite_addr}] send failed ({e}): {text}")
            if sock not in readable:
                continue

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
            # 正しいTMを送ってきた相手を衛星とみなし、TCの送り先にする
            satellite_addr = addr

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
