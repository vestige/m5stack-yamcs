# experiments/failover の start.sh / stop.sh / chaos.sh から読み込む共通設定
# shellcheck shell=bash

FAILOVER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# リポジトリの scripts/common.sh にある find_java_home / find_python / running_pid を借りる
# (RUN_DIR などはこのあと実験用に置き換える)
# shellcheck disable=SC1091
source "$FAILOVER_DIR/../../scripts/common.sh"

RUN_DIR="$FAILOVER_DIR/.run"
LOG_DIR="$FAILOVER_DIR/logs"
PRIMARY_URL="http://127.0.0.1:8190"
STANDBY_URL="http://127.0.0.1:8191"
# 系ごとの HTTP のポート。YAMCS の JVM を見つけるのに使う
node_port() { [[ "$1" == primary ]] && echo 8190 || echo 8191; }
# 系のプロセス (mvnw と、yamcs:run が別に起動した YAMCS の JVM)。
# JVM のコマンドラインには failover/<系> が入らないので、待ち受けているポートから探す
node_pids() {
  { pgrep -f "failover/$1" || true; lsof -t -nP -iTCP:"$(node_port "$1")" -sTCP:LISTEN 2>/dev/null || true; } | sort -u
}
NODES=(primary standby)
