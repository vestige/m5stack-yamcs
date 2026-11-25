"""パスの手順 (#56, #69): データレコーダの記録を区切って降ろし (ダンプ)、降ろせたことを確かめた所まで消す。

Timeline のアクティビティとして、通信の確認のあとに自動で動く (tools/plan_passes.py)。
左メニューの Scripts から手で動かすこともできる。YAMCS が渡す環境変数 YAMCS_URL と YAMCS_INSTANCE を使う。

ダンプは約 4KB/秒 (ファームウェアの DUMP_INTERVAL_MS と DUMP_BYTES_PER_STEP) なので、2分のパスで降ろせるのは
数百 KB まで。長くパスがなかった分を一度に降ろそうとすると終わらないので、解放の印 (降ろし済みの所) から
区切りながら降ろし、数がそろった区切りまで印を進める。パスを重ねるごとに、たまった分が少しずつ減る。

区切り1つの手順:
1. DUMP_RECORDER: 解放の印 (開始=0) から、区切りの終わり (End) までを降ろす
2. 衛星の ACK (コマンド履歴の CommandComplete) を待つ。そのあとに地上が受け取った HK で、降ろす量を見る。
   残りの時間に収まらなければ STOP_DUMP して、区切りを短くしてやり直す
3. HK の DumpState が IDLE に戻る (衛星が送り終える) まで待つ
4. 衛星が送った数 (HK の DumpSent) と、地上が受け取った数を比べる。地上が受け取った数は、ダンプのリンクが受け取った数に、
   受信スクリプトが「リアルタイムで受け取り済み」として YAMCS に送らなかったイベントの数 (M5SAT_DUMP_STATS のファイル) を足したもの
5. 同じなら CLEAR_RECORDER で、区切りの終わりまでを消す (解放の印が進む)。足りなければ消さずに失敗で終わる
   (次のパスで、同じ所からもう一度降ろす)

衛星の時計 (機上時刻) と地上の時計は、ぴったりとは合っていない (2秒ほどずれていたことがある、#69)。
なので「コマンドのあとの HK か」は、衛星が HK を作った時刻ではなく、地上が受け取った時刻 (acquisitionTime) で決める。

本物の運用でも、地上で受け取れたことを確かめるまで、衛星の記録は消さない (消した記録は二度と降ろせない)。
"""

import datetime
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DUMP_LINK = "m5stack-dump"
# パスは2分ほど。通信の確認のあとに残った時間のうち、この秒数までダンプに使う。超えたら残りは次のパスに回す
BUDGET_SECONDS = 90
# 衛星がダンプを送る速さ (KB/秒)。ファームウェアの DUMP_INTERVAL_MS = 100、DUMP_BYTES_PER_STEP = 400 から
DUMP_KB_PER_SECOND = 4.0
# 区切りの長さ (記録した時間の秒数) の初め。ふだんの記録 (HK と GNSS を1秒ごと) は 1秒あたり 0.2KB ほどなので、
# 10分ぶんで 120KB (降ろすのに 30秒) ほど。実際の量を見て、次の区切りの長さを変える
FIRST_CHUNK_SECONDS = 600
# 降ろす量の見積もりに対する余裕 (残りの時間のこの割合まで使う)
SAFETY = 0.8
# コマンドの ACK や HK を待つ時間
COMMAND_TIMEOUT = 10
# 区切り1つで、ダンプを送る時間のほかにかかる秒数 (DUMP_RECORDER と CLEAR_RECORDER の往復、届ききるのを待つ時間)
OVERHEAD_SECONDS = 8
# 次の区切りに入れられる量がこれより少なくなったら、そのパスのダンプは終わりにする
MIN_CHUNK_KB = 20

URL = os.environ.get("YAMCS_URL", "http://localhost:8090").rstrip("/")
INSTANCE = os.environ.get("YAMCS_INSTANCE", "gnss")
# 受信スクリプト (tools/udp_receiver.py --dump-stats) が書くファイル。scripts/start.sh が渡す
DUMP_STATS = os.environ.get("M5SAT_DUMP_STATS")


class Failed(Exception):
    pass


def request(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(f"{URL}/api{path}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req) as res:
        text = res.read()
        return json.loads(text) if text else {}


def parse_time(text):
    return datetime.datetime.fromisoformat(text.replace("Z", "+00:00"))


def iso(t):
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def hk(name):
    """HK のパラメータの今の値と、地上がそれを受け取った時刻"""
    value = request(f"/processors/{INSTANCE}/realtime/parameters/M5Sat/Bus/{name}")
    eng = value.get("engValue", {})
    v = eng.get("stringValue", eng.get("uint32Value"))
    acquired = value.get("acquisitionTime")
    return v, parse_time(acquired) if acquired else None


def hk_after(name, t, timeout=COMMAND_TIMEOUT):
    """地上が t より後に受け取った HK の値を返す (コマンドの結果が HK に出るのを待つ)"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        v, acquired = hk(name)
        if acquired and acquired > t:
            return v
        time.sleep(0.3)
    raise Failed(f"{timeout}秒たっても HK が届きません")


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
    """コマンドを送り、衛星の ACK (実行し終えた) を待つ。ACK を地上が受け取った時刻を返す"""
    print(f"{name} {args}")
    issued = request(f"/processors/{INSTANCE}/realtime/commands/M5Sat/Bus/{name}", {"args": args})
    quoted = urllib.parse.quote(issued["id"], safe="")
    deadline = time.time() + COMMAND_TIMEOUT
    while time.time() < deadline:
        time.sleep(0.3)
        try:
            entry = request(f"/archive/{INSTANCE}/commands/{quoted}")
        except urllib.error.HTTPError:
            continue  # コマンド履歴に入るまで少し待つ
        attrs = {a["name"]: a.get("value", {}).get("stringValue") for a in entry.get("attr", [])}
        status = attrs.get("CommandComplete_Status")
        if status == "OK":
            return parse_time(attrs["CommandComplete_Time"])
        if status == "NOK":
            raise Failed(f"{name} が衛星で失敗しました: {attrs.get('CommandComplete_Message', '')}")
    raise Failed(f"{COMMAND_TIMEOUT}秒たっても {name} の ACK が届きません")


def released():
    """解放の印 (この時刻より前は、降ろせたと確かめて消した)。bus と gnss の早いほう"""
    marks = [parse_time(hk(f"Recorder{s}Released")[0]) for s in ("Bus", "Gnss")]
    return min(marks)


def usable_kb(deadline):
    """残りの時間で降ろせる量 (KB) の見積もり"""
    return max(0.0, (deadline - time.time() - OVERHEAD_SECONDS) * SAFETY * DUMP_KB_PER_SECOND)


def plan_kb(deadline):
    """次の区切りに入れる量 (KB)。DUMP_RECORDER を送って量を確かめるまでにも時間がたつので、その分と余裕を引いておく。
    ちょうどの量にすると、確かめるときには少しはみ出して、止めて縮めるのをくり返してしまう"""
    return max(0.0, (deadline - time.time() - 2 * OVERHEAD_SECONDS) * SAFETY * DUMP_KB_PER_SECOND * 0.85)


def dump_chunk(end, deadline):
    """解放の印から end までを降ろす。(降ろした量 KB、区切りの中の量 KB) を返す。時間に収まらないときは降ろした量が None"""
    began = time.time()
    before = received()
    # 開始 1970-01-01T00:00:00Z (0) は「解放の印から」
    acked = command("DUMP_RECORDER", {"Source": "ALL", "Start": "1970-01-01T00:00:00Z", "End": iso(end)})
    state = hk_after("DumpState", acked)
    total_kb = int(hk_after("DumpRemainingKB", acked) or 0) if state == "DUMPING" else 0
    if total_kb > usable_kb(deadline):
        stop_acked = command("STOP_DUMP", {})
        hk_after("DumpState", stop_acked)
        # 止める前に送られたパケットが届ききるのを待つ (次の区切りの「受け取った数」に混ざらないように)
        time.sleep(2)
        print(f"{total_kb} KB は残りの時間で降ろしきれないので、止めて区切りを短くします")
        return None, total_kb

    while state == "DUMPING":
        if time.time() > deadline:
            command("STOP_DUMP", {})
            raise Failed("時間内に降ろしきれませんでした。消さずに、次のパスでもう一度降ろします")
        time.sleep(1)
        state, _ = hk("DumpState")

    # 最後のダンプのパケットが地上に届き、HK に送った数が出るのを少し待つ
    time.sleep(2)
    sent = int(hk("DumpSent")[0] or 0)
    got = received() - before
    print(f"{iso(end)} まで: 衛星が送った数 {sent}、地上が受け取った数 {got} ({total_kb} KB、{time.time() - began:.0f}秒)")
    if got < sent:
        raise Failed(f"{sent - got} 個のパケットが届いていません。消さずに、次のパスでもう一度降ろします")

    command("CLEAR_RECORDER", {"Source": "ALL", "Before": iso(end)})
    print(f"{iso(end)} より前の記録を、降ろし終えた分として消しました")
    return total_kb, total_kb


def main():
    state, _ = hk("DumpState")
    if state == "DUMPING":
        sys.exit("衛星はほかのダンプの途中です")

    deadline = time.time() + BUDGET_SECONDS
    goal = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
    chunk = FIRST_CHUNK_SECONDS
    try:
        while True:
            # 残りの時間で降ろせる量が少なければ、区切りを作っても往復の時間ばかりかかるので、次のパスに回す
            if plan_kb(deadline) < MIN_CHUNK_KB:
                print("時間になったので、残りは次のパスで降ろします")
                return
            start = released()
            if start.year < 2000:
                # まだ一度も消していない (印が無い) ので区切れない。全部を一度に降ろす
                end = goal
            else:
                end = min(goal, start + datetime.timedelta(seconds=chunk))
            dumped_kb, total_kb = dump_chunk(end, deadline)
            span = (end - start).total_seconds()
            if dumped_kb is None:
                # 区切りの中の量から、残りの時間 (やり直しの往復も見込む) に1回で収まる長さに縮める
                chunk = max(10, int(span * plan_kb(deadline) / max(total_kb, 1)))
                continue
            if end >= goal:
                print("今までの記録を、すべて降ろしました")
                return
            # 今の区切りの量から、残りの時間に収まる長さにする (量が少なければ長く、多ければ短く)
            density = dumped_kb / span if span > 0 else 0
            chunk = max(10, int(plan_kb(deadline) / density)) if density > 0 else chunk * 4
    except Failed as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
