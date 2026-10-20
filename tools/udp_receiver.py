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
    APID 103 イベント        lib/TelemetryPacket/src/EventPacket.h
    APID 104 観測データ      lib/TelemetryPacket/src/ObservationPacket.h
    APID 110 TC             lib/Satellite/src/Telecommand.h
"""

import argparse
import datetime
import os
import select
import socket
import struct
import termios
import time

PRIMARY_HEADER = struct.Struct(">HHH")

APID_GNSS = 100
APID_HOUSEKEEPING = 101
APID_ACK = 102
APID_EVENT = 103
APID_OBSERVATION = 104
APID_TELECOMMAND = 110

# time, ms, lat, lon, alt, sats, fixQuality, fixType, flags, uptime, ok, err
GNSS_PAYLOAD = struct.Struct(">IHddfBBBBIII")
# mode, payloadPower, accepted, rejected, lastCommand, rssi, freeHeap, minFreeHeap, uptime, bootCount, resetReason,
# fdirEnabled, lastFault, faultCount, onboardTime, observationState, observationStart, observationDuration,
# lastObservationResult, lastObservationStart, lastObservationLength, completedObservations
HOUSEKEEPING_PAYLOAD = struct.Struct(">BBHHBbIIIHBBBHIBIHBIHH")
# tcSequence, commandId, stage, errorCode
ACK_PAYLOAD = struct.Struct(">HBBB")
# severity, eventId, arg1, arg2
EVENT_PAYLOAD = struct.Struct(">BBHH")
# utc, count, tracked, maxCn0, avgCn0, GPS, GLONASS, Galileo, BeiDou, QZSS
OBSERVATION_HEADER = struct.Struct(">IBBBBBBBBB")
# system, prn, elevation, azimuth, cn0
OBSERVATION_ENTRY = struct.Struct(">BBbHB")
GNSS_SYSTEM = {0: "G", 1: "R", 2: "E", 3: "C", 4: "J", 5: "I"}
OBSERVATION_STATE = {0: "NONE", 1: "SCHEDULED", 2: "ACTIVE"}
OBSERVATION_RESULT = {0: "NONE", 1: "COMPLETED", 2: "ABORTED", 3: "SKIPPED_PAYLOAD_OFF",
                      4: "SKIPPED_NOT_NOMINAL", 5: "CANCELLED"}

FLAG_TIME_VALID = 1 << 0
FLAG_DATE_VALID = 1 << 1
FLAG_POSITION_VALID = 1 << 2
FLAG_ALTITUDE_VALID = 1 << 3
FLAG_RMC_ACTIVE = 1 << 4

FIX_TYPE = {1: "NoFix", 2: "2D", 3: "3D"}
FIX_QUALITY = {0: "NoFix", 1: "GPS", 2: "DGPS", 3: "PPS", 4: "RTK", 5: "FloatRTK", 6: "Estimated"}
MODE = {0: "SAFE", 1: "NOMINAL", 2: "MISSION"}
COMMAND = {
    1: "NO_OP",
    2: "SET_MODE",
    3: "SET_TM_INTERVAL",
    4: "PAYLOAD_POWER",
    5: "RESET_COUNTERS",
    6: "BEEP",
    7: "SET_FDIR",
    8: "SCHEDULE_OBSERVATION",
    9: "CANCEL_OBSERVATION",
}
FAULT = {0: "NONE", 1: "GNSS_NO_DATA"}
ACK_STAGE = {1: "ACCEPTED", 2: "REJECTED", 3: "COMPLETED", 4: "FAILED"}
ERROR_CODE = {
    0: "NONE",
    1: "UNKNOWN_COMMAND",
    2: "INVALID_LENGTH",
    3: "INVALID_ARGUMENT",
    4: "NOT_ALLOWED",
    5: "MALFORMED_PACKET",
}
EVENT_SEVERITY = {0: "INFO", 1: "WATCH", 2: "WARNING", 3: "DISTRESS", 4: "CRITICAL", 5: "SEVERE"}
EVENT_ID = {
    1: "BOOT",
    2: "MODE_CHANGED",
    3: "PAYLOAD_POWER_CHANGED",
    4: "TM_INTERVAL_CHANGED",
    5: "COUNTERS_RESET",
    6: "COMMAND_REJECTED",
    7: "FAULT_DETECTED",
    8: "FDIR_CHANGED",
    9: "OBSERVATION_SCHEDULED",
    10: "OBSERVATION_STARTED",
    11: "OBSERVATION_ENDED",
    12: "OBSERVATION_SKIPPED",
    13: "OBSERVATION_CANCELLED",
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
     reset_reason, fdir, last_fault, fault_count, onboard_time, obs_state, obs_start,
     obs_duration, last_result, last_start, last_length, completed) = unpack_payload(HOUSEKEEPING_PAYLOAD, data)
    return (
        f"HK   mode={MODE.get(mode, mode)} payload={'ON' if payload else 'OFF'} "
        f"cmd(ok={accepted} ng={rejected} last={COMMAND.get(last_command, 'NONE')}) rssi={rssi}dBm "
        f"heap={free_heap}(min {min_free_heap}) uptime={uptime / 1000:.1f}s "
        f"boot={boot_count} reset={RESET_REASON.get(reset_reason, reset_reason)} "
        f"fdir={'ON' if fdir else 'OFF'} fault={FAULT.get(last_fault, last_fault)}x{fault_count} "
        f"time={format_utc(onboard_time)} obs={OBSERVATION_STATE.get(obs_state, obs_state)}"
        + (f"({format_utc(obs_start)} {obs_duration}s)" if obs_state else "")
        + f" last_obs={OBSERVATION_RESULT.get(last_result, last_result)}"
        + (f"({format_utc(last_start)} {last_length}s)" if last_result else "")
        + f" done={completed}"
    )


def format_utc(seconds):
    if not seconds:
        return "--:--:--"
    return datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc).strftime("%H:%M:%SZ")


def format_observation(data):
    if len(data) < PRIMARY_HEADER.size + OBSERVATION_HEADER.size:
        raise ValueError(f"packet too short: {len(data)} bytes")
    utc, count, tracked, max_cn0, avg_cn0, *per_system = OBSERVATION_HEADER.unpack_from(data, PRIMARY_HEADER.size)
    offset = PRIMARY_HEADER.size + OBSERVATION_HEADER.size
    if len(data) != offset + count * OBSERVATION_ENTRY.size:
        raise ValueError(f"unexpected packet size: {len(data)} bytes for {count} satellites")
    top = []
    for i in range(min(count, 5)):
        system, prn, _elevation, _azimuth, cn0 = OBSERVATION_ENTRY.unpack_from(data, offset + i * OBSERVATION_ENTRY.size)
        top.append(f"{GNSS_SYSTEM.get(system, '?')}{prn:02d}:{cn0}")
    return (
        f"OBS  {format_utc(utc)} view={count} tracked={tracked} cn0(max {max_cn0} avg {avg_cn0}) "
        f"GRECJ={'/'.join(str(n) for n in per_system)} top={' '.join(top)}"
    )


def format_ack(data):
    tc_seq, command_id, stage, error = unpack_payload(ACK_PAYLOAD, data)
    text = f"ACK  tc_seq={tc_seq} {COMMAND.get(command_id, command_id)} {ACK_STAGE.get(stage, stage)}"
    if error:
        text += f" error={ERROR_CODE.get(error, error)}"
    return text


def format_event(data):
    severity, event_id, arg1, arg2 = unpack_payload(EVENT_PAYLOAD, data)
    name = EVENT_ID.get(event_id, event_id)
    if name == "BOOT":
        detail = f"boot={arg1} reset={RESET_REASON.get(arg2, arg2)}"
    elif name == "MODE_CHANGED":
        detail = f"{MODE.get(arg1, arg1)} -> {MODE.get(arg2, arg2)}"
    elif name == "PAYLOAD_POWER_CHANGED":
        detail = "ON" if arg1 else "OFF"
    elif name == "TM_INTERVAL_CHANGED":
        detail = f"{arg1}ms"
    elif name == "COMMAND_REJECTED":
        detail = f"{COMMAND.get(arg1, arg1)} {ERROR_CODE.get(arg2, arg2)}"
    elif name == "FAULT_DETECTED":
        detail = f"{FAULT.get(arg1, arg1)} {arg2}s"
    elif name == "FDIR_CHANGED":
        detail = "ON" if arg1 else "OFF"
    elif name == "OBSERVATION_SCHEDULED":
        detail = f"in {arg1}s for {arg2}s"
    elif name == "OBSERVATION_STARTED":
        detail = f"{arg1}s"
    elif name == "OBSERVATION_ENDED":
        detail = "completed" if arg1 == 0 else "interrupted"
    elif name == "OBSERVATION_SKIPPED":
        detail = "payload off" if arg1 == 1 else f"not nominal ({MODE.get(arg2, arg2)})"
    elif name == "OBSERVATION_CANCELLED":
        detail = "while active" if arg1 else "scheduled"
    else:
        detail = f"arg1={arg1} arg2={arg2}"
    return f"EVT  {EVENT_SEVERITY.get(severity, severity)} {name} {detail}"


def format_telecommand(data):
    header = decode_header(data)
    command_id = data[PRIMARY_HEADER.size] if len(data) > PRIMARY_HEADER.size else None
    args = data[PRIMARY_HEADER.size + 1:]
    return f"TC   seq={header['seq']} {COMMAND.get(command_id, command_id)} args={args.hex() or '-'}"


FORMATTERS = {
    APID_GNSS: format_gnss,
    APID_HOUSEKEEPING: format_housekeeping,
    APID_ACK: format_ack,
    APID_EVENT: format_event,
    APID_OBSERVATION: format_observation,
}


SYNC_MARKER = bytes([0x1A, 0xCF, 0xFC, 0x1D])
MAX_FRAME_PAYLOAD = 256


def crc16(data, crc=0xFFFF):
    """CRC-16-CCITT (初期値 0xFFFF, 多項式 0x1021)。lib/SerialFrame と同じ"""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(payload):
    body = struct.pack(">H", len(payload)) + payload
    return SYNC_MARKER + body + struct.pack(">H", crc16(body))


class FrameDecoder:
    """シリアルのバイトの流れから、ASMで区切られたフレームを取り出す"""

    def __init__(self):
        self.buffer = bytearray()
        self.crc_errors = 0

    def feed(self, data):
        self.buffer += data
        frames = []
        while True:
            start = self.buffer.find(SYNC_MARKER)
            if start < 0:
                # 印の先頭が途中まで届いているかもしれないので、末尾3バイトは残す
                del self.buffer[:-3]
                return frames
            del self.buffer[:start]
            if len(self.buffer) < 6:
                return frames
            (size,) = struct.unpack_from(">H", self.buffer, 4)
            if size > MAX_FRAME_PAYLOAD:
                del self.buffer[:1]
                continue
            if len(self.buffer) < 6 + size + 2:
                return frames
            body = bytes(self.buffer[4:6 + size])
            (crc,) = struct.unpack_from(">H", self.buffer, 6 + size)
            if crc == crc16(body):
                frames.append(body[2:])
                del self.buffer[:6 + size + 2]
            else:
                self.crc_errors += 1
                del self.buffer[:1]


def open_serial(path, baud):
    """pyserial を使わずに、シリアルポートを raw モードで開く"""
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0                                                   # iflag
    attrs[1] = 0                                                   # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL        # cflag
    attrs[3] = 0                                                   # lflag
    speed = getattr(termios, f"B{baud}")
    attrs[4] = attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="0.0.0.0", help="待ち受けるアドレス (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=10015, help="待ち受けるUDPポート (default: 10015)")
    parser.add_argument("--raw", action="store_true", help="受信したバイト列も16進で表示する")
    parser.add_argument("--forward", metavar="HOST:PORT", help="受信したパケットをそのまま転送する先 (例: YAMCS)")
    parser.add_argument("--uplink-port", type=int, help="YAMCSからTCを受け取る 127.0.0.1 のUDPポート (例: 10025)")
    parser.add_argument("--satellite-tc-port", type=int, default=10025, help="衛星がTCを待ち受けるポート (default: 10025)")
    parser.add_argument("--serial", metavar="PATH", help="地上局の無線機 (M5StickC) のシリアルポート")
    parser.add_argument("--baud", type=int, default=115200, help="シリアルの速度 (default: 115200)")
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
    satellite_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if args.uplink_port:
        # YAMCSからは 127.0.0.1 で受け取り、衛星へは別のソケットで送る
        # (127.0.0.1 に結び付けたソケットからはLAN上の相手に送れない)
        uplink_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        uplink_sock.bind(("127.0.0.1", args.uplink_port))
        print(f"Uplinking TC from udp://127.0.0.1:{args.uplink_port} to the satellite")
    serial_fd = None
    decoder = FrameDecoder()
    serial_retry_at = 0.0
    serial_retrying = False

    def connect_serial():
        """地上局の無線機のシリアルを開く。USBが抜かれていたら、あとでもう一度試す"""
        nonlocal serial_fd, serial_retry_at, serial_retrying, decoder
        try:
            serial_fd = open_serial(args.serial, args.baud)
            decoder = FrameDecoder()
            serial_retrying = False
            print(f"Listening on serial {args.serial} ({args.baud} baud, ESP-NOW ground station)")
        except OSError as e:
            serial_fd = None
            serial_retry_at = time.monotonic() + 2.0
            # 抜かれている間、ログが同じ行で埋まらないよう最初の1回だけ出す
            if not serial_retrying:
                print(f"[serial] cannot open {args.serial} ({e.strerror}), retrying every 2s")
            serial_retrying = True

    def disconnect_serial(reason):
        nonlocal serial_fd, serial_retry_at, satellite_route
        print(f"[serial] {args.serial} disconnected ({reason})")
        try:
            os.close(serial_fd)
        except OSError:
            pass
        serial_fd = None
        serial_retry_at = time.monotonic() + 2.0
        if satellite_route == ("serial", None):
            satellite_route = None

    # シーケンス番号は送り主とAPIDごとに数えられている
    last_seq = {}
    # 衛星へTCを送る経路: ("udp", IPアドレス) または ("serial", None)
    satellite_route = None
    if args.serial:
        connect_serial()

    def handle_telemetry(data, origin, route):
        nonlocal satellite_route
        if forward_sock:
            # 中身の検査は転送先(YAMCS)に任せ、届いたものはすべて転送する。
            # 転送先が止まっていても受信は続ける
            try:
                forward_sock.sendto(data, forward_to)
            except OSError:
                pass
        if args.raw:
            print(f"[{origin}] {data.hex(' ')}")

        try:
            header = decode_header(data)
            formatter = FORMATTERS.get(header["apid"])
            if formatter is None:
                raise ValueError(f"unknown APID {header['apid']}")
            text = formatter(data)
        except ValueError as e:
            print(f"[{origin}] invalid packet: {e}")
            return
        # 正しいTMが届いた経路を、TCの送り先にする
        satellite_route = route

        # シーケンス番号の飛びからパケットロスを検出する(14bitで一周する)
        apid = header["apid"]
        key = (origin, apid)
        if key in last_seq:
            lost = (header["seq"] - last_seq[key] - 1) & 0x3FFF
            if lost:
                print(f"[{origin}] apid={apid}: {lost} packet(s) lost")
        last_seq[key] = header["seq"]

        print(f"[{origin}] apid={apid} seq={header['seq']:5d} {text}")

    def uplink(tc):
        try:
            text = format_telecommand(tc)
        except ValueError as e:
            text = f"TC   (invalid: {e})"
        if satellite_route is None:
            print(f"[uplink] satellite not in view, dropped: {text}")
            return
        kind, addr = satellite_route
        target = addr if kind == "udp" else "serial"
        try:
            if kind == "udp":
                satellite_sock.sendto(tc, (addr, args.satellite_tc_port))
            elif serial_fd is None:
                raise OSError("ground station is disconnected")
            else:
                os.write(serial_fd, encode_frame(tc))
            print(f"[uplink -> {target}] {text}")
        except OSError as e:
            print(f"[uplink -> {target}] send failed ({e}): {text}")

    try:
        while True:
            if args.serial and serial_fd is None and time.monotonic() >= serial_retry_at:
                connect_serial()
            sources = [s for s in (sock, uplink_sock, serial_fd) if s is not None]
            readable, _, _ = select.select(sources, [], [], 1.0)
            if uplink_sock in readable:
                tc, _ = uplink_sock.recvfrom(2048)
                uplink(tc)
            if sock in readable:
                data, (addr, _) = sock.recvfrom(2048)
                handle_telemetry(data, addr, ("udp", addr))
            if serial_fd is not None and serial_fd in readable:
                try:
                    chunk = os.read(serial_fd, 4096)
                except BlockingIOError:
                    continue
                except OSError as e:
                    disconnect_serial(e.strerror)
                    continue
                if not chunk:
                    # 読める状態なのに0バイト → USBが抜かれた
                    disconnect_serial("end of file")
                    continue
                for frame in decoder.feed(chunk):
                    handle_telemetry(frame, "esp-now", ("serial", None))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
