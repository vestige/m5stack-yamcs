"""Timeline のアクティビティから呼び、衛星とのデータリンク (TM と TC) を有効・無効にする。

パスの始め (AOS) に `set_links.py enable`、終わり (LOS) に `set_links.py disable` を実行すると、
パスの外では衛星と通信できなくなる。YAMCS が渡す環境変数 YAMCS_URL と YAMCS_INSTANCE を使う。

enable のときは、テレメトリが実際に届き始めるまで待つ (AOS = 衛星の信号をつかんだ)。
WAIT_SECONDS 秒たっても届かなければ失敗で終わり、そのあとの手順 (通信の確認) は始まらない。
"""

import json
import os
import sys
import time
import urllib.request

TM_LINK = "m5stack-gnss"
LINKS = [TM_LINK, "m5stack-tc"]
WAIT_SECONDS = 15

URL = os.environ.get("YAMCS_URL", "http://localhost:8090").rstrip("/")
INSTANCE = os.environ.get("YAMCS_INSTANCE", "gnss")


def request(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"{URL}/api{path}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as res:
        text = res.read()
        return json.loads(text) if text else {}


def received():
    return int(request(f"/links/{INSTANCE}/{TM_LINK}").get("dataInCount", 0))


def main():
    if len(sys.argv) != 2 or sys.argv[1] not in ("enable", "disable"):
        sys.exit("usage: set_links.py enable|disable")
    action = sys.argv[1]
    before = received()
    for link in LINKS:
        request(f"/links/{INSTANCE}/{link}:{action}", {})
        print(f"{link}: {action}d")
    if action == "disable":
        return

    deadline = time.time() + WAIT_SECONDS
    while time.time() < deadline:
        time.sleep(1)
        if received() > before:
            print("テレメトリが届き始めた (AOS)")
            return
    sys.exit(f"{WAIT_SECONDS}秒たってもテレメトリが届かない")


if __name__ == "__main__":
    main()
