#!/usr/bin/env bash
# Mac側のプログラム (YAMCS、UDP受信スクリプト、Grafana) をバックグラウンドで起動する
#
#   M5Stack → :10015 udp_receiver.py (ログ記録) → :10016 YAMCS → http://localhost:8090
#   M5Stack ← :10025 udp_receiver.py (TCの中継) ← :10025 YAMCS
#   M5Stack ~ ESP-NOW ~ M5StickC ─USB─ udp_receiver.py   (.env に GROUND_STATION_SERIAL を書いたとき)
#
# ログ: logs/yamcs.log, logs/udp_receiver.log, logs/grafana.log
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
  local serial_args=()
  if [[ -n "$GROUND_STATION_SERIAL" ]]; then
    if [[ ! -e "$GROUND_STATION_SERIAL" ]]; then
      # 受信スクリプトは、USBがあとからつながったら自動でつなぎ直す
      echo "udp_receiver: 地上局の無線機 $GROUND_STATION_SERIAL がまだつながっていません。つながったら自動で受信を始めます" >&2
    fi
    serial_args=(--serial "$GROUND_STATION_SERIAL")
  fi
  echo "udp_receiver: starting (TM udp :$RECEIVER_PORT -> :$YAMCS_TM_PORT, TC udp :$UPLINK_PORT -> satellite${GROUND_STATION_SERIAL:+, ESP-NOW $GROUND_STATION_SERIAL})"
  nohup "$python" -u "$ROOT_DIR/tools/udp_receiver.py" --port "$RECEIVER_PORT" \
    --forward "127.0.0.1:$YAMCS_TM_PORT" --uplink-port "$UPLINK_PORT" ${serial_args[@]+"${serial_args[@]}"} \
    >"$(log_file udp_receiver)" 2>&1 &
  echo $! >"$(pid_file udp_receiver)"

  sleep 1
  if ! running_pid udp_receiver >/dev/null; then
    echo "udp_receiver: 起動に失敗しました。ポート$RECEIVER_PORT / $UPLINK_PORTを他のプログラムが使っていないか確認してください" >&2
    cat "$(log_file udp_receiver)" >&2
    exit 1
  fi
}

start_grafana() {
  if pid="$(running_pid grafana)"; then
    echo "grafana: already running (pid $pid)"
    return 0
  fi
  if [[ ! -x "$GRAFANA_HOME/bin/grafana" ]]; then
    echo "grafana: まだ準備していないので起動しません (使うときは scripts/setup_grafana.sh)"
    return 0
  fi
  echo "grafana: starting ($GRAFANA_VERSION)"
  GF_PATHS_DATA="$GRAFANA_DIR/data" \
    GF_PATHS_PLUGINS="$GRAFANA_DIR/data/plugins" \
    GF_PATHS_LOGS="$LOG_DIR/grafana" \
    GF_PATHS_PROVISIONING="$GRAFANA_DIR/provisioning" \
    GF_DASHBOARDS_DEFAULT_HOME_DASHBOARD_PATH="$GRAFANA_DIR/dashboards/m5sat-trends.json" \
    M5SAT_GRAFANA_DASHBOARDS="$GRAFANA_DIR/dashboards" \
    nohup "$GRAFANA_HOME/bin/grafana" server --homepath "$GRAFANA_HOME" --config "$GRAFANA_DIR/grafana.ini" \
    >"$(log_file grafana)" 2>&1 &
  echo $! >"$(pid_file grafana)"
  echo -n "grafana: waiting for $GRAFANA_URL "
  local i
  for ((i = 0; i < 60; i++)); do
    if curl -sf "$GRAFANA_URL/api/health" >/dev/null 2>&1; then
      echo " ready"
      return 0
    fi
    if ! running_pid grafana >/dev/null; then
      echo " failed"
      echo "grafana: 起動に失敗しました。ログを確認してください: $(log_file grafana)" >&2
      tail -20 "$(log_file grafana)" >&2
      exit 1
    fi
    echo -n "."
    sleep 1
  done
  echo " timeout"
}

start_yamcs
start_udp_receiver
start_grafana

cat <<EOF

起動しました。
  YAMCS Web UI : $YAMCS_URL
  Grafana      : $GRAFANA_URL  (scripts/setup_grafana.sh で準備したとき)
  受信ログ     : tail -f logs/udp_receiver.log
  YAMCSログ    : tail -f logs/yamcs.log
  停止         : scripts/stop.sh

M5Stackの送信先 (include/secrets.h の TELEMETRY_HOST) はこのMacのIPアドレスにしてください: $(ipconfig getifaddr en0 2>/dev/null || echo "?")
EOF
