#!/usr/bin/env bash
# 冗長化の実験環境を起動する: Active 系 YAMCS (:8190)、Standby 系 YAMCS (:8191)、模擬の衛星・地上局
#
#   fake_sat.py ──TM udp:10116──▶ primary (:8190) ──レプリケーション tcp:8099──▶ standby (:8191)
#               ◀─TC udp:10125──                                               (TM udp:10216 / TC udp:10126 は無効)
#
# いつもの scripts/start.sh (ポート 8090) とは別のポートを使うので、同時に動かしてもぶつからない。
# ログ: experiments/failover/logs/
set -euo pipefail
source "$(dirname "$0")/common.sh"
mkdir -p "$RUN_DIR" "$LOG_DIR"

java_home="$(find_java_home)" || { echo "Java 17以上が見つかりません ('brew install openjdk@17')" >&2; exit 1; }

# MDB と processor.yaml は本体 (yamcs/src/main/yamcs) のものをそのまま使う
for node in "${NODES[@]}"; do
  mkdir -p "$FAILOVER_DIR/$node/src/main/yamcs/mdb"
  cp "$ROOT_DIR/yamcs/src/main/yamcs/mdb/m5sat.xml" "$FAILOVER_DIR/$node/src/main/yamcs/mdb/"
  cp "$ROOT_DIR/yamcs/src/main/yamcs/etc/processor.yaml" "$FAILOVER_DIR/$node/src/main/yamcs/etc/"
done

start_node() {
  local node="$1"
  if pid="$(running_pid "$node")"; then
    echo "$node: already running (pid $pid)"
    return 0
  fi
  echo "$node: starting"
  # mvnw は yamcs/ の .mvn を使うので、yamcs/ から -f で実験用の pom を指す
  (
    cd "$ROOT_DIR/yamcs"
    JAVA_HOME="$java_home" nohup ./mvnw -B -f "$FAILOVER_DIR/$node/pom.xml" yamcs:run >"$(log_file "$node")" 2>&1 &
    echo $! >"$(pid_file "$node")"
  )
}

wait_node() {
  local node="$1" i
  echo -n "$node: waiting "
  for ((i = 0; i < 300; i++)); do
    if grep -q "Yamcs started" "$(log_file "$node")" 2>/dev/null; then echo " ready"; return 0; fi
    if ! running_pid "$node" >/dev/null; then echo " failed"; tail -20 "$(log_file "$node")" >&2; exit 1; fi
    echo -n "."; sleep 2
  done
  echo " timeout"; exit 1
}

start_node primary
wait_node primary
start_node standby
wait_node standby

if pid="$(running_pid fake_sat)"; then
  echo "fake_sat: already running (pid $pid)"
else
  echo "fake_sat: starting (接続先: $(cat "$RUN_DIR/target" 2>/dev/null || echo primary))"
  nohup "$(find_python)" -u "$FAILOVER_DIR/tools/fake_sat.py" --run-dir "$RUN_DIR" --log-dir "$LOG_DIR" \
    >"$(log_file fake_sat)" 2>&1 &
  echo $! >"$(pid_file fake_sat)"
fi

echo
echo "Active  系: $PRIMARY_URL   Standby 系: $STANDBY_URL"
echo "死活監視:   python3 $FAILOVER_DIR/tools/watchdog.py"
