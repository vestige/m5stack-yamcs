#!/usr/bin/env bash
# Grafana の公式の配布ファイルを grafana/dist/ に展開し、YAMCS のプラグインを入れる (#21)
#
# Homebrew や Docker は使わない。Mac の環境を汚さず、フォルダを消せば元どおりになる。
# 版は scripts/common.sh の GRAFANA_VERSION / GRAFANA_PLUGIN_VERSION で決める
set -euo pipefail
source "$(dirname "$0")/common.sh"

case "$(uname -m)" in
  arm64) arch=arm64 ;;
  x86_64) arch=amd64 ;;
  *) echo "このMacのCPU ($(uname -m)) 向けの配布ファイルがありません" >&2; exit 1 ;;
esac

if [[ -x "$GRAFANA_HOME/bin/grafana" ]]; then
  echo "grafana: $GRAFANA_VERSION は展開済み ($GRAFANA_HOME)"
else
  tarball="grafana-$GRAFANA_VERSION.darwin-$arch.tar.gz"
  url="https://dl.grafana.com/oss/release/$tarball"
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  echo "grafana: downloading $url"
  curl -fL --progress-bar -o "$tmp/$tarball" "$url"
  expected="$(curl -fsL "$url.sha256")"
  actual="$(shasum -a 256 "$tmp/$tarball" | cut -d' ' -f1)"
  if [[ "$expected" != "$actual" ]]; then
    echo "grafana: チェックサムが合いません (expected $expected, got $actual)" >&2
    exit 1
  fi
  mkdir -p "$GRAFANA_DIR/dist"
  tar -xzf "$tmp/$tarball" -C "$tmp"
  # 配布ファイルの中のフォルダ名 (grafana-v13.2.4 など) にかかわらず、grafana-<版> に置く
  mv "$tmp"/grafana-*/ "$GRAFANA_HOME"
  echo "grafana: $GRAFANA_HOME に展開しました"
fi

plugin_dir="$GRAFANA_DIR/data/plugins/yamcs-yamcs-datasource"
if [[ -f "$plugin_dir/plugin.json" ]] && grep -q "\"version\": \"$GRAFANA_PLUGIN_VERSION\"" "$plugin_dir/plugin.json"; then
  echo "grafana: プラグイン yamcs-yamcs-datasource $GRAFANA_PLUGIN_VERSION は入れてあります"
else
  zip="yamcs-yamcs-datasource-$GRAFANA_PLUGIN_VERSION.zip"
  url="https://github.com/yamcs/grafana-yamcs/releases/download/v$GRAFANA_PLUGIN_VERSION/$zip"
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' EXIT
  echo "grafana: downloading $url"
  curl -fL --progress-bar -o "$tmp/$zip" "$url"
  expected="$(curl -fsL "$url.md5" | cut -d' ' -f1)"
  actual="$(md5 -q "$tmp/$zip")"
  if [[ "$expected" != "$actual" ]]; then
    echo "grafana: プラグインのチェックサムが合いません (expected $expected, got $actual)" >&2
    exit 1
  fi
  rm -rf "$plugin_dir"
  mkdir -p "$(dirname "$plugin_dir")"
  unzip -q "$tmp/$zip" -d "$(dirname "$plugin_dir")"
  echo "grafana: プラグインを $plugin_dir に入れました"
fi

echo "準備ができました。scripts/start.sh で YAMCS といっしょに起動します ($GRAFANA_URL)"
