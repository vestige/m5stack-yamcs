#!/usr/bin/env python3
"""データレコーダ (#55) が microSD に記録したファイルを読み、受信ログと同じ形で表示する。

使い方:
    python3 tools/read_recording.py /Volumes/M5SAT/rec/bus/20261030/07.bin
    python3 tools/read_recording.py /Volumes/M5SAT/rec              # フォルダの中を全部 (機器・時刻の順)
    python3 tools/read_recording.py /Volumes/M5SAT/rec --summary    # 機器・ファイルごとの件数と時間の範囲だけ

--summary では、HK に入っている起動の回数 (boot) も出す。1つのファイルに boot が2つあれば、
その時間の途中で再起動して、再起動の前の記録の後ろに書き足したということ

microSD の中の形 (lib/Recorder/src/RecordFormat.h):
    /rec/<機器>/<年月日>/<時>.bin       機器: bus (HK・ACK・イベント)、gnss (GNSS・観測データ)
    ファイルの中は「長さ (uint16、ビッグエンディアン) + パケットそのまま」の繰り返し
"""

import argparse
import datetime
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import udp_receiver as rx  # noqa: E402  パケットの表示は受信スクリプトと同じにする

LENGTH = struct.Struct(">H")


def records(path):
    """ファイルの中の (パケット) を順に返す。最後が書きかけで切れていたら、そこで止める"""
    data = path.read_bytes()
    offset = 0
    while offset + LENGTH.size <= len(data):
        (size,) = LENGTH.unpack_from(data, offset)
        offset += LENGTH.size
        if offset + size > len(data):
            print(f"  ({path.name}: 最後の記録が途中で切れています。{len(data) - offset}/{size} バイト)")
            return
        yield data[offset:offset + size]
        offset += size


def files(root):
    root = pathlib.Path(root)
    if root.is_file():
        return [root]
    return sorted(root.rglob("*.bin"))


def iso(time_ms):
    return datetime.datetime.fromtimestamp(time_ms / 1000, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("path", help="記録したファイル (.bin) か、そのフォルダ (/Volumes/M5SAT/rec など)")
    parser.add_argument("--summary", action="store_true", help="ファイルごとの件数と時間の範囲だけを出す")
    args = parser.parse_args()

    targets = files(args.path)
    if not targets:
        sys.exit(f"{args.path} に記録 (.bin) が見つかりません")
    for path in targets:
        count = 0
        first = last = None
        bad = 0
        boots = []
        for packet in records(path):
            count += 1
            try:
                header = rx.decode_header(packet)
                formatter = rx.FORMATTERS.get(header["apid"])
                text = formatter(packet) if formatter else f"unknown APID {header['apid']}"
            except ValueError as e:
                bad += 1
                text = f"invalid packet: {e}"
                header = {"apid": "?", "seq": 0, "time_ms": None}
            if header["apid"] == rx.APID_HOUSEKEEPING:
                boot = rx.HOUSEKEEPING_PAYLOAD.unpack_from(packet, rx.payload_offset(packet))[9]
                if boot not in boots:
                    boots.append(boot)
            t = header.get("time_ms")
            if t:
                first = t if first is None else min(first, t)
                last = t if last is None else max(last, t)
            if not args.summary:
                print(f"[{path.parent.parent.name}] apid={header['apid']} seq={header['seq']:5d} "
                      f"{rx.format_packet_time(t)}{text}")
        span = f"{iso(first)} 〜 {iso(last)}" if first else "時刻なし"
        boot_text = f"、boot {', '.join(map(str, boots))}" if boots else ""
        print(f"== {path}: {count} 件 ({span}){boot_text}" + (f"、読めないもの {bad} 件" if bad else ""))


if __name__ == "__main__":
    main()
