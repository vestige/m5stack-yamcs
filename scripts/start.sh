#!/usr/bin/env bash
# Mac側のプログラム (YAMCS と UDP受信スクリプト) をバックグラウンドで起動する
#
#   M5Stack → :10015 udp_receiver.py (ログ記録) → :10016 YAMCS → http://localhost:8090
#
# ログ: logs/yamcs.log, logs/udp_receiver.log
set -euo pipefail
source "$(dirname "$0")/common.sh"

mkdir -p "$RUN_DIR" "$LOG_DIR"

start_yamcs() {
  if pid="$(running_pid yamcs)"; then
    echo "yamcs: already running (pid $pid)"
    return 0
  fi

  local java_home
  if ! java_home="$(find_java_home)"; then
    echo "yamcs: Java 17以上が見つかりません。'brew install openjdk@17' でインストールしてください" >&2
    exit 1
  fi

  echo "yamcs: starting (JAVA_HOME=$java_home)"
  (
    cd "$ROOT_DIR/yamcs"
    JAVA_HOME="$java_home" nohup ./mvnw -B yamcs:run >"$(log_file yamcs)" 2>&1 &
    echo $! >"$(pid_file yamcs)"
  )

  # 初回は依存ライブラリのダウンロードで数分かかる
  echo -n "yamcs: waiting for $YAMCS_URL "
  local i
  for ((i = 0; i < 300; i++)); do
    if grep -q "Yamcs started" "$(log_file yamcs)" 2>/dev/null; then
      echo " ready"
      return 0
    fi
    if ! running_pid yamcs >/dev/null; then
      echo " failed"
      echo "yamcs: 起動に失敗しました。ログを確認してください: $(log_file yamcs)" >&2
      tail -20 "$(log_file yamcs)" >&2
      exit 1
    fi
    echo -n "."
    sleep 2
  done
  echo " timeout"
  echo "yamcs: 起動を待ちきれませんでした。ログを確認してください: $(log_file yamcs)" >&2
  exit 1
}

start_udp_receiver() {
  if pid="$(running_pid udp_receiver)"; then
    echo "udp_receiver: already running (pid $pid)"
    return 0
  fi

  local python
  python="$(find_python)"
  echo "udp_receiver: starting (udp :$RECEIVER_PORT -> :$YAMCS_TM_PORT)"
  nohup "$python" -u "$ROOT_DIR/tools/udp_receiver.py" --port "$RECEIVER_PORT" \
    --forward "127.0.0.1:$YAMCS_TM_PORT" >"$(log_file udp_receiver)" 2>&1 &
  echo $! >"$(pid_file udp_receiver)"

  sleep 1
  if ! running_pid udp_receiver >/dev/null; then
    echo "udp_receiver: 起動に失敗しました。ポート$RECEIVER_PORTを他のプログラムが使っていないか確認してください" >&2
    cat "$(log_file udp_receiver)" >&2
    exit 1
  fi
}

start_yamcs
start_udp_receiver

cat <<EOF

起動しました。
  YAMCS Web UI : $YAMCS_URL
  受信ログ     : tail -f logs/udp_receiver.log
  YAMCSログ    : tail -f logs/yamcs.log
  停止         : scripts/stop.sh

M5Stackの送信先 (include/secrets.h の TELEMETRY_HOST) はこのMacのIPアドレスにしてください: $(ipconfig getifaddr en0 2>/dev/null || echo "?")
EOF
