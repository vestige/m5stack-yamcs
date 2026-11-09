"""watchdog.py / failover.py / check_history.py で共通に使う、YAMCS の HTTP API の呼び出し。

Python の標準ライブラリだけで動く。すべての呼び出しにタイムアウトを付ける
(止まった YAMCS に呼び出しが引っかかったままにならないように。実験 E2・E5 で効いてくる)。
"""

import json
import os
import urllib.error
import urllib.parse
import urllib.request

INSTANCE = "m5sat"
NODES = {
    "primary": os.environ.get("PRIMARY_URL", "http://127.0.0.1:8190"),
    "standby": os.environ.get("STANDBY_URL", "http://127.0.0.1:8191"),
}
TM_LINK = "sat-tm"
TC_LINK = "sat-tc"
UPTIME_PARAMETER = "M5Sat/Bus/Uptime"
ACCEPTED_PARAMETER = "M5Sat/Bus/AcceptedCommands"


class ApiError(Exception):
    pass


def call(node, path, body=None, timeout=1.5):
    """node: "primary" / "standby"。失敗したら ApiError (接続できない・タイムアウト・HTTP エラー)"""
    url = NODES[node] + "/api" + path
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"},
                                 method="POST" if body is not None else "GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as res:
            text = res.read()
            return json.loads(text) if text else {}
    except urllib.error.HTTPError as e:
        raise ApiError(f"HTTP {e.code}: {e.read()[:200]!r}") from e
    except (urllib.error.URLError, OSError) as e:  # 接続拒否・タイムアウト
        raise ApiError(f"{type(e).__name__}: {e}") from e


def link(node, name, timeout=1.5):
    return call(node, f"/links/{INSTANCE}/{name}", timeout=timeout)


def set_link(node, name, enable, timeout=2.0):
    action = "enable" if enable else "disable"
    return call(node, f"/links/{INSTANCE}/{name}:{action}", body={}, timeout=timeout)


def parameter(node, name, timeout=1.5):
    """realtime プロセッサの今の値 (engValue の値)。まだ値がなければ None"""
    res = call(node, f"/processors/{INSTANCE}/realtime/parameters/{name}", timeout=timeout)
    value = res.get("engValue") or {}
    for key in ("uint32Value", "sint32Value", "uint64Value", "sint64Value", "floatValue", "doubleValue", "stringValue"):
        if key in value:
            return int(value[key]) if key.endswith(("32Value", "64Value")) else value[key]
    return None


def replication(node, timeout=1.5):
    return call(node, "/replication", timeout=timeout)


REPLICATION_SLAVE_CLASS = "org.yamcs.replication.ReplicationSlave"


def find_service(node, class_name, timeout=1.5):
    """インスタンスのサービスから、クラス名で探す (GET /api/services/{instance})。
    サービス名の既定値はドキュメントに書かれていないので、名前ではなくクラス名で見つける。見つからなければ None"""
    res = call(node, f"/services/{INSTANCE}", timeout=timeout)
    for service in res.get("services", []):
        if service.get("className") == class_name:
            return service
    return None


def stop_service(node, name, timeout=2.0):
    """POST /api/services/{instance}/{name}:stop。止めたサービスは再開できない (開始すると作り直される)。
    また、YAMCS を再起動すると設定どおりにまた動き出す"""
    quoted = urllib.parse.quote(name, safe="")
    return call(node, f"/services/{INSTANCE}/{quoted}:stop", body={}, timeout=timeout)
