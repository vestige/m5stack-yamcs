#!/usr/bin/env bash
# start.sh で起動したプログラムの状態と、YAMCSのデータリンクの受信数を表示する
set -euo pipefail
source "$(dirname "$0")/common.sh"

for name in "${SERVICES[@]}"; do
  if pid="$(running_pid "$name")"; then
    echo "$name: running (pid $pid)"
  else
    echo "$name: stopped"
  fi
done

if links="$(curl -sf "$YAMCS_URL/api/links/gnss" 2>/dev/null)"; then
  echo "$links" | "$(find_python)" -c '
import json, sys
for link in json.load(sys.stdin).get("links", []):
    print("yamcs link %s: %s, packets in=%s" % (link["name"], link["status"], link.get("dataInCount", 0)))
'
fi
