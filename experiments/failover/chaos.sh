#!/usr/bin/env bash
# 障害を起こす (実験用)。どちらの系にも使える
#
#   ./chaos.sh kill   primary   プロセスを強制終了 (サーバの停止・ハードウェア障害に相当)
#   ./chaos.sh hang   primary   プロセスを一時停止 (応答しないがポートは開いたまま。ハングアップに相当)
#   ./chaos.sh resume primary   一時停止を解く
#   ./chaos.sh restart primary  止めてから起動しなおす (落ちたプロセスを自動で起動しなおす仕組みに相当。実験 E7)
#   ./chaos.sh start  standby   止めた系を起動する
set -euo pipefail
source "$(dirname "$0")/common.sh"

action="${1:-}"; node="${2:-}"
[[ "$node" == primary || "$node" == standby ]] || { sed -n 2,9p "$0"; exit 1; }
stamp() { date "+%H:%M:%S"; }

case "$action" in
  kill)
    pids="$(node_pids "$node")"
    [[ -n "$pids" ]] || { echo "$node: プロセスがない"; exit 1; }
    kill -CONT $pids 2>/dev/null || true
    kill -9 $pids && echo "[$(stamp)] $node: 強制終了した (pid $(echo $pids))" ;;
  hang)
    pids="$(node_pids "$node")"
    [[ -n "$pids" ]] || { echo "$node: プロセスがない"; exit 1; }
    kill -STOP $pids && echo "[$(stamp)] $node: 一時停止した (pid $(echo $pids)。resume で戻す)" ;;
  resume)
    pids="$(node_pids "$node")"
    [[ -n "$pids" ]] || { echo "$node: プロセスがない"; exit 1; }
    kill -CONT $pids && echo "[$(stamp)] $node: 再開した" ;;
  start|restart)
    if [[ "$action" == restart ]]; then
      pids="$(node_pids "$node")"
      if [[ -n "$pids" ]]; then
        kill -CONT $pids 2>/dev/null || true
        kill $pids 2>/dev/null || true
      fi
      sleep 3
    fi
    rm -f "$(pid_file "$node")"
    echo "[$(stamp)] $node: 起動する"
    java_home="$(find_java_home)"
    (
      cd "$ROOT_DIR/yamcs"
      JAVA_HOME="$java_home" nohup ./mvnw -B -f "$FAILOVER_DIR/$node/pom.xml" yamcs:run >>"$(log_file "$node")" 2>&1 &
      echo $! >"$(pid_file "$node")"
    )
    echo "  起動の完了は logs/$node.log の「Yamcs started」で確かめる" ;;
  *)
    sed -n 2,9p "$0"; exit 1 ;;
esac
