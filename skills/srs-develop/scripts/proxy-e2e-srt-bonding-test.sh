#!/bin/bash
# E2E test of a bonded SRT publish through the Go proxy: runs the srs-integ
# cases TestTimer_Srt_ProxyBonding (cmd/integ/cases/srt_proxy_bonding_test.go),
# which start the SRS origins, the proxy and the impairment relays themselves:
#   - the proxy with the memory load balancer and two origins with srt_server.bonding on;
#   - a broadcast or backup group of the libsrt v1.5.7 client publishes through the proxy, and both
#     links reach the one origin picked for the stream, where the group is one publisher;
#   - a gosrt player through the proxy gets every message once;
#   - one link blackholed mid-stream: the stream goes on, with no gap and no unpublish;
#   - two proxies sharing Redis, one link of the group through each: both links reach one origin, and the
#     stream goes on over the link through one proxy when the link through the other is blackholed.
# Several proxies need Redis for bonding: with the memory load balancer each proxy pins a stream on its own,
# so the links of a group through two proxies may reach two origins.
#
# Usage:
#   bash skills/srs-develop/scripts/proxy-e2e-srt-bonding-test.sh [go test args after -args, such as -integ.known-bugs=fail]
#
# Needs Go, lsof, Redis (redis-cli; started with brew services on macOS when it is not running),
# trunk/objs/srs (its libsrt built with bonding, as configure does) and the test media (make integ-prepare);
# builds bin/srs-proxy and tools/srt-libsrt when they are missing or stale.
#
# Exit code is 0 when every case passes, 1 otherwise.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Walk up from SCRIPT_DIR looking for go.mod, as the other proxy-e2e scripts do.
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done
if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

# SRS_TEST_PORT_BASE, by default this script's slot in the Port Plan, moves every port. The cases take their
# ports from the integ allocator, SRS_INTEG_PORTS, set to this slot: the main process's part of it, its first
# 80 ports.
PORT_BASE=${SRS_TEST_PORT_BASE:-16700}
PORT_LAST=$((PORT_BASE + 99))

REDIS_HOST="${PROXY_REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${PROXY_REDIS_PORT:-6379}"
REDIS_KEY_PREFIX="e2e-srt-bond-$(LC_ALL=C tr -dc 'a-f0-9' </dev/urandom | head -c 8)"

SRS_BINARY="$WORKSPACE/trunk/objs/srs"
PROXY_BINARY="$WORKSPACE/bin/srs-proxy"

redis_cli() {
  redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" "$@"
}

# Kill what listens on this slot's ports, never a client of them.
kill_slot_listeners() {
  local pids
  pids="$(lsof -nP -t -iTCP:"$PORT_BASE-$PORT_LAST" -sTCP:LISTEN 2>/dev/null || true)"
  pids="$pids $(lsof -nP -iUDP:"$PORT_BASE-$PORT_LAST" 2>/dev/null | awk 'NR > 1 && $9 !~ /->/ {print $2}' || true)"
  pids="$(echo $pids | tr ' ' '\n' | sort -u)"
  [[ -z "$pids" ]] && return
  echo "Stopping stale processes on $PORT_BASE-$PORT_LAST: $(echo $pids)"
  kill $pids 2>/dev/null || true
  sleep 1
  kill -9 $pids 2>/dev/null || true
}

# Remove only this run's keys: the prefix is new for each run.
cleanup_redis_keys() {
  local keys
  keys="$(redis_cli --scan --pattern "$REDIS_KEY_PREFIX:*" 2>/dev/null || true)"
  if [[ -n "$keys" ]]; then
    echo "$keys" | xargs redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" del >/dev/null 2>&1 || true
  fi
  echo "Cleaned $(echo -n "$keys" | grep -c . || true) Redis key(s) of $REDIS_KEY_PREFIX."
}

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  kill_slot_listeners
  cleanup_redis_keys
  echo "Cleanup done."
}
trap cleanup EXIT

echo "=== E2E SRT Proxy Bonding Test ==="
echo "Workspace: $WORKSPACE"
echo "Ports:     $PORT_BASE-$PORT_LAST"
echo "Redis:     $REDIS_HOST:$REDIS_PORT, key prefix $REDIS_KEY_PREFIX"
echo ""

# --- Pre-checks ---
for cmd in go lsof redis-cli; do
  if ! command -v "$cmd" &>/dev/null; then
    echo "Error: $cmd not found in PATH" >&2
    exit 1
  fi
done
if [[ ! -x "$SRS_BINARY" ]]; then
  echo "Error: no SRS at $SRS_BINARY; build it as cmd/integ/README.md says:" >&2
  echo "  cd trunk && ./configure --srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on && make" >&2
  exit 1
fi
if ! redis_cli ping 2>/dev/null | grep -q "PONG" && command -v brew &>/dev/null; then
  echo "Redis is not running, starting it with: brew services start redis"
  brew services start redis
  for i in $(seq 1 10); do
    redis_cli ping 2>/dev/null | grep -q "PONG" && break
    sleep 1
  done
fi
if ! redis_cli ping 2>/dev/null | grep -q "PONG"; then
  echo "Error: Redis is not available at $REDIS_HOST:$REDIS_PORT" >&2
  exit 1
fi

kill_slot_listeners

# --- Step 1: Build the proxy, the libsrt client and the test media ---
echo "=== Step 1: Building the proxy, tools/srt-libsrt and the test media ==="
cd "$WORKSPACE"
make -s bin/srs-proxy
bash "$SCRIPT_DIR/srt-libsrt-build.sh" >/dev/null
make -s integ-prepare >/dev/null

# --- Step 2: Run the cases ---
echo "=== Step 2: Running TestTimer_Srt_ProxyBonding ==="
START=$(date +%s)
SRT_PROXY="$PROXY_BINARY" \
  SRT_PROXY_REDIS="$REDIS_HOST:$REDIS_PORT" \
  SRT_PROXY_REDIS_PREFIX="$REDIS_KEY_PREFIX" \
  SRS_INTEG_PORTS="$PORT_BASE-$PORT_LAST" \
  go -C "$WORKSPACE/cmd/integ/cases" test -count=1 -run '^TestTimer_Srt_ProxyBonding$' -args "$@"
echo "Cases took $(($(date +%s) - START))s."

echo ""
echo "=== E2E SRT Proxy Bonding Test PASSED ==="
