"""パスの手順 (#56): データレコーダの記録を降ろし (ダンプ)、降ろせたことを確かめてから消す。

Timeline のアクティビティとして、通信の確認のあとに自動で動く (tools/plan_passes.py)。
Procedures から手で動かすこともできる。YAMCS が渡す環境変数 YAMCS_URL と YAMCS_INSTANCE を使う。

1. DUMP_RECORDER: まだ消していない分の始めから、今までを降ろす
2. HK の DumpState が IDLE に戻る (衛星が送り終える) まで待つ
3. 衛星が送った数 (HK の DumpSent) と、地上が受け取った数を比べる。地上が受け取った数は、ダンプのリンクが受け取った数に、
   受信スクリプトが「リアルタイムで受け取り済み」として YAMCS に送らなかったイベントの数 (M5SAT_DUMP_STATS のファイル) を足したもの
4. 同じなら CLEAR_RECORDER で、降ろした分を消す。足りなければ消さずに失敗で終わる (次のパスでもう一度降ろす)

本物の運用でも、地上で受け取れたことを確かめるまで、衛星の記録は消さない (消した記録は二度と降ろせない)。
"""

import datetime
import json
import os
import sys
import time
import urllib.request

DUMP_LINK = "m5stack-dump"
# パスは2分ほど。通信の確認のあとに残った時間で降ろしきれなければ、あきらめて次のパスに回す
WAIT_SECONDS = 90

URL = os.environ.get("YAMCS_URL", "http://localhost:8090").rstrip("/")
INSTANCE = os.environ.get("YAMCS_INSTANCE", "gnss")
# 受信スクリプト (tools/udp_receiver.py --dump-stats) が書くファイル。scripts/start.sh が渡す
DUMP_STATS = os.environ.get("M5SAT_DUMP_STATS")


def request(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"{URL}/api{path}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as res:
        text = res.read()
        return json.loads(text) if text else {}


def parameter(name):
    value = request(f"/processors/{INSTANCE}/realtime/parameters/M5Sat/Bus/{name}")
    eng = value.get("engValue", {})
    return eng.get("stringValue", eng.get("uint32Value")), value.get("generationTime")


def received():
    """地上が受け取ったダンプのパケットの数 (これまでの合計)"""
    count = int(request(f"/links/{INSTANCE}/{DUMP_LINK}").get("dataInCount", 0))
    return count + skipped_events()


def skipped_events():
    """リアルタイムで受け取り済みなので、受信スクリプトが YAMCS に送らなかったイベントの数 (これまでの合計)"""
    if not DUMP_STATS:
        return 0
    try:
        with open(DUMP_STATS) as f:
            return int(json.load(f).get("skipped_events", 0))
    except (OSError, ValueError):
        return 0


def command(name, args):
    request(f"/processors/{INSTANCE}/realtime/commands/M5Sat/Bus/{name}", {"args": args})
    print(f"{name} {args}")


def wait_for_hk_after(t):
    """t より後に作られた HK が届くまで待つ (コマンドの結果が HK に出るのを待つ)"""
    deadline = time.time() + 10
    while time.time() < deadline:
        _, generated = parameter("DumpState")
        if generated and generated > t:
            return
        time.sleep(0.5)


def main():
    state, _ = parameter("DumpState")
    if state == "DUMPING":
        sys.exit("衛星はほかのダンプの途中です")

    end = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
    end_text = end.strftime("%Y-%m-%dT%H:%M:%SZ")
    before = received()
    # 開始 1970-01-01T00:00:00Z (0) は「まだ消していない分の始めから」
    command("DUMP_RECORDER", {"Source": "ALL", "Start": "1970-01-01T00:00:00Z", "End": end_text})
    wait_for_hk_after(end_text)

    deadline = time.time() + WAIT_SECONDS
    while True:
        state, _ = parameter("DumpState")
        if state == "IDLE":
            break
        if time.time() > deadline:
            command("STOP_DUMP", {})
            sys.exit(f"{WAIT_SECONDS}秒で降ろしきれませんでした。消さずに、次のパスでもう一度降ろします")
        time.sleep(1)

    # 最後のダンプのパケットが地上に届き、HK に送った数が出るのを少し待つ
    time.sleep(2)
    sent, _ = parameter("DumpSent")
    got = received() - before
    print(f"衛星が送った数 {sent}、地上が受け取った数 {got}")
    if got < int(sent):
        sys.exit(f"{int(sent) - got} 個のパケットが届いていません。消さずに、次のパスでもう一度降ろします")

    command("CLEAR_RECORDER", {"Source": "ALL", "Before": end_text})
    print(f"{end_text} より前の記録を、降ろし終えた分として消しました")


if __name__ == "__main__":
    main()
