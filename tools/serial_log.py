#!/usr/bin/env python3
"""M5Stack のシリアル出力を、時刻つきでファイルに記録する。

使い方:
    python3 tools/serial_log.py /dev/cu.usbserial-XXXX logs/m5stack-serial.log

ウォッチドッグなどで再起動したときのメッセージ (Guru Meditation, Backtrace) を残すのに使う。
USB が抜かれても、つなぎ直されるまで待って記録を続ける。
"""

import datetime
import os
import sys
import termios
import time


def open_serial(path, baud=115200):
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0
    attrs[4] = attrs[5] = getattr(termios, f"B{baud}")
    attrs[6][termios.VMIN] = 1
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    path, out_path = sys.argv[1], sys.argv[2]

    with open(out_path, "a", buffering=1) as out:
        while True:
            try:
                fd = open_serial(path)
            except OSError:
                time.sleep(2)
                continue
            out.write(f"--- {datetime.datetime.now().isoformat(timespec='seconds')} opened {path}\n")
            line = b""
            try:
                while True:
                    chunk = os.read(fd, 1024)
                    if not chunk:
                        break
                    line += chunk
                    while b"\n" in line:
                        text, line = line.split(b"\n", 1)
                        stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        out.write(f"{stamp} {text.decode(errors='replace').rstrip()}\n")
            except OSError as e:
                out.write(f"--- {datetime.datetime.now().isoformat(timespec='seconds')} closed ({e.strerror})\n")
            finally:
                os.close(fd)


if __name__ == "__main__":
    main()
