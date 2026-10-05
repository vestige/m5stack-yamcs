#!/usr/bin/env bash
# start.sh で起動したプログラムを終了する
set -euo pipefail
source "$(dirname "$0")/common.sh"

stop_service() {
  local name=$1 pid
  if ! pid="$(running_pid "$name")"; then
    echo "$name: not running"
    return 0
  fi

  echo -n "$name: stopping (pid $pid) "
  kill_tree "$pid" TERM

  # YAMCSはデータを保存してから終了するので少し待つ
  local i
  for ((i = 0; i < 30; i++)); do
    if ! kill -0 "$pid" 2>/dev/null; then
      rm -f "$(pid_file "$name")"
      echo " stopped"
      return 0
    fi
    echo -n "."
    sleep 1
  done

  kill_tree "$pid" KILL
  rm -f "$(pid_file "$name")"
  echo " killed"
}

# 起動と逆の順で止める
for ((i = ${#SERVICES[@]} - 1; i >= 0; i--)); do
  stop_service "${SERVICES[i]}"
done
