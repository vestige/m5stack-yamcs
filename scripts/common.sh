# start.sh / stop.sh / status.sh から読み込む共通設定
# shellcheck shell=bash

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_DIR="$ROOT_DIR/.run"
LOG_DIR="$ROOT_DIR/logs"

# M5Stackが送ってくるポート (include/secrets.h の TELEMETRY_PORT)
RECEIVER_PORT=10015
# YAMCSのUDPデータリンクのポート (yamcs/src/main/yamcs/etc/yamcs.gnss.yaml)
YAMCS_TM_PORT=10016
# YAMCSがTCを送るポート。受信スクリプトが受け取って衛星へ中継する (yamcs.gnss.yaml)
UPLINK_PORT=10025
YAMCS_URL="http://localhost:8090"

# 管理するプロセス (起動順)
SERVICES=(yamcs udp_receiver)

pid_file() { echo "$RUN_DIR/$1.pid"; }
log_file() { echo "$LOG_DIR/$1.log"; }

# PIDファイルのプロセスが動いていればPIDを出力する
running_pid() {
  local file
  file="$(pid_file "$1")"
  [[ -f "$file" ]] || return 1
  local pid
  pid="$(cat "$file")"
  if kill -0 "$pid" 2>/dev/null; then
    echo "$pid"
    return 0
  fi
  rm -f "$file"
  return 1
}

# 子プロセスも含めて終了させる (mvnw は YAMCS を別のJVMで起動するため)
kill_tree() {
  local pid=$1 signal=${2:-TERM} child
  for child in $(pgrep -P "$pid" 2>/dev/null); do
    kill_tree "$child" "$signal"
  done
  kill "-$signal" "$pid" 2>/dev/null || true
}

find_java_home() {
  if [[ -n "${JAVA_HOME:-}" && -x "$JAVA_HOME/bin/java" ]]; then
    echo "$JAVA_HOME"
    return 0
  fi
  local candidate
  for candidate in /opt/homebrew/opt/openjdk@17 /usr/local/opt/openjdk@17; do
    if [[ -x "$candidate/bin/java" ]]; then
      echo "$candidate"
      return 0
    fi
  done
  /usr/libexec/java_home -v 17+ 2>/dev/null
}

# asdf などで python3 が使えない場合は macOS 標準の python3 を使う
find_python() {
  if command -v python3 >/dev/null 2>&1 && (cd "$ROOT_DIR" && python3 -c "" >/dev/null 2>&1); then
    command -v python3
  else
    echo /usr/bin/python3
  fi
}
