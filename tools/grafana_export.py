#!/usr/bin/env python3
"""Grafana の画面で直したダッシュボードを、grafana/dashboards/ の JSON に書き出す (#21)。

使い方:
    python3 tools/grafana_export.py                 # M5Sat トレンド (uid: m5sat-trends) を書き出す
    python3 tools/grafana_export.py --uid <uid>     # ほかのダッシュボード (ファイル名は <uid>.json)

Grafana の画面で保存しただけでは、Grafana の中 (grafana/data/grafana.db) にしか残らない。
このスクリプトでファイルに書き出してから、git でコミットする。
ダッシュボードはログインなしで見られる設定 (grafana/grafana.ini の auth.anonymous) なので、パスワードは要らない。
"""

import argparse
import json
import pathlib
import sys
import urllib.error
import urllib.request

DASHBOARDS = pathlib.Path(__file__).resolve().parent.parent / "grafana" / "dashboards"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--uid", default="m5sat-trends", help="ダッシュボードの uid (default: m5sat-trends)")
    parser.add_argument("--url", default="http://localhost:3000", help="GrafanaのURL")
    args = parser.parse_args()

    req = urllib.request.Request(f"{args.url}/api/dashboards/uid/{args.uid}")
    try:
        with urllib.request.urlopen(req) as res:
            dashboard = json.load(res)["dashboard"]
    except urllib.error.HTTPError as e:
        sys.exit(f"{args.uid} を取り出せません: {e.code} {e.read().decode()}")
    except urllib.error.URLError as e:
        sys.exit(f"Grafana につながりません ({args.url}): {e.reason}")

    # Grafana の中での番号と版は、ファイルには要らない (読み込むたびに付け直される)
    dashboard.pop("id", None)
    dashboard.pop("version", None)
    path = DASHBOARDS / f"{args.uid}.json"
    path.write_text(json.dumps(dashboard, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{path} に書き出しました。git diff で変更を確かめてからコミットしてください")


if __name__ == "__main__":
    main()
