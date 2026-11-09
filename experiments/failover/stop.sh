#!/usr/bin/env bash
# 実験環境を止める。--clean を付けると、保存データ (target/) とログ・状態も消して最初からにする
set -euo pipefail
source "$(dirname "$0")/common.sh"

for name in fake_sat standby primary; do
  if pid="$(running_pid "$name")"; then
    echo "$name: stopping (pid $pid)"
    kill "$pid" 2>/dev/null || true
    rm -f "$(pid_file "$name")"
  fi
done
# yamcs:run が別に起動した JVM も止める (chaos.sh で一時停止していても止まるように CONT してから)
for node in "${NODES[@]}"; do
  pids="$(node_pids "$node")"
  [[ -n "$pids" ]] || continue
  kill -CONT $pids 2>/dev/null || true
  kill $pids 2>/dev/null || true
done

if [[ "${1:-}" == "--clean" ]]; then
  rm -rf "$FAILOVER_DIR/primary/target" "$FAILOVER_DIR/standby/target" "$RUN_DIR" "$LOG_DIR"
  echo "保存データとログを消しました"
fi
