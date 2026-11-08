#!/usr/bin/env bash
# Ensure the pymobiledevice3 RSD tunnel daemon is running. Idempotent: safe to call from every iOS script. The tunnel needs root for the
# network interface, so this uses `sudo` (one password prompt) and backgrounds the daemon.
#
#   bash scripts/ios/ios-tunnel.sh          # bring it up if it isn't
#   bash scripts/ios/ios-tunnel.sh --stop   # kill it
#
# Env: PYMOBILEDEVICE3_TUNNELD_URL (default http://127.0.0.1:49151)
set -euo pipefail

URL="${PYMOBILEDEVICE3_TUNNELD_URL:-http://127.0.0.1:49151}"
LOG="${XF_TUNNELD_LOG:-/tmp/xf-tunneld.log}"
PMD3="$(command -v pymobiledevice3 || true)"
[ -n "$PMD3" ] || { echo "ios-tunnel: pymobiledevice3 not on PATH" >&2; exit 1; }

if [ "${1:-}" = "--stop" ]; then
  sudo pkill -f "pymobiledevice3 remote tunneld" && echo "tunnel stopped" || echo "no tunnel running"
  exit 0
fi

if curl -sf "$URL" >/dev/null 2>&1; then
  echo "ios-tunnel: already up ($URL)"
  exit 0
fi

echo "ios-tunnel: starting RSD tunnel (sudo)…"
sudo -b sh -c "'$PMD3' remote tunneld >'$LOG' 2>&1"

for _ in $(seq 1 30); do
  if curl -sf "$URL" >/dev/null 2>&1; then
    echo "ios-tunnel: up ($URL)"
    exit 0
  fi
  sleep 1
done

echo "ios-tunnel: did not come up within 30s — see $LOG" >&2
exit 1
