#!/bin/bash
# E2E test for RTMP proxy origin-cluster routing: starts one proxy + two SRS
# origins, publishes multiple RTMP streams, verifies playback via proxy, and
# verifies that different streams are assigned to different origin servers.
set -e

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Navigate: scripts/ -> srs-develop/ -> skills/ -> .openclaw/ -> srs
WORKSPACE="$(cd -P "$SCRIPT_DIR/../../../.." && pwd)"

if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found in WORKSPACE: $WORKSPACE" >&2
  exit 1
fi

# Ports — use high ports to avoid conflicts with running services.
# The proxy starts ALL servers, so we must assign unique ports for each.
PROXY_RTMP_PORT=11935
PROXY_HTTP_API_PORT=11985
PROXY_HTTP_SERVER_PORT=18080
PROXY_WEBRTC_PORT=18000
PROXY_SRT_PORT=20080
PROXY_SYSTEM_API_PORT=12025

SOURCE_FLV="$WORKSPACE/trunk/doc/source.flv"
SRS_BINARY="$WORKSPACE/trunk/objs/srs"

# Origin ports from origin1-for-proxy.conf and origin2-for-proxy.conf.
ORIGIN1_RTMP_PORT=19351
ORIGIN1_HTTP_PORT=8081
ORIGIN1_API_PORT=19851
ORIGIN1_RTC_PORT=8001
ORIGIN1_SRT_PORT=10081

ORIGIN2_RTMP_PORT=19352
ORIGIN2_HTTP_PORT=8082
ORIGIN2_API_PORT=19853
ORIGIN2_RTC_PORT=8002
ORIGIN2_SRT_PORT=10082

# PIDs to clean up on exit.
PROXY_PID=""
ORIGIN_PIDS=()
FFMPEG_PIDS=()

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  for pid in $PROXY_PID "${ORIGIN_PIDS[@]}" "${FFMPEG_PIDS[@]}"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done
  sleep 1
  for pid in $PROXY_PID "${ORIGIN_PIDS[@]}" "${FFMPEG_PIDS[@]}"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done
  echo "Cleanup done."
}
trap cleanup EXIT

wait_for_http() {
  local url=$1
  local name=$2
  local i

  for i in $(seq 1 30); do
    if curl -fsS --max-time 2 "$url" >/dev/null 2>&1; then
      echo "$name is ready."
      return 0
    fi
    sleep 1
  done

  echo "Error: $name is not ready after 30s: $url" >&2
  return 1
}

origin_has_stream() {
  local api_port=$1
  local stream=$2

  curl -fsS --max-time 3 "http://127.0.0.1:$api_port/api/v1/streams/" 2>/dev/null | grep -q "$stream"
}

detect_origin_for_stream() {
  local stream=$1
  local i

  for i in $(seq 1 10); do
    local on_origin1=0
    local on_origin2=0

    if origin_has_stream "$ORIGIN1_API_PORT" "$stream"; then
      on_origin1=1
    fi
    if origin_has_stream "$ORIGIN2_API_PORT" "$stream"; then
      on_origin2=1
    fi

    if [[ $on_origin1 -eq 1 && $on_origin2 -eq 0 ]]; then
      echo "origin1"
      return 0
    fi
    if [[ $on_origin1 -eq 0 && $on_origin2 -eq 1 ]]; then
      echo "origin2"
      return 0
    fi
    if [[ $on_origin1 -eq 1 && $on_origin2 -eq 1 ]]; then
      echo "Error: stream $stream exists on both origins; expected exactly one owner" >&2
      return 1
    fi

    sleep 1
  done

  echo "Error: stream $stream was not found on either origin" >&2
  return 1
}

verify_probe_has_av() {
  local url=$1
  local label=$2
  local probe_output

  probe_output=$(ffprobe -v error -rw_timeout 5000000 -show_streams "$url" 2>&1 || true)

  if ! echo "$probe_output" | grep -q "codec_type=video"; then
    echo "FAIL: No video stream detected for $label." >&2
    echo "ffprobe output:" >&2
    echo "$probe_output" >&2
    exit 1
  fi

  if ! echo "$probe_output" | grep -q "codec_type=audio"; then
    echo "FAIL: No audio stream detected for $label." >&2
    echo "ffprobe output:" >&2
    echo "$probe_output" >&2
    exit 1
  fi

  echo "PASS: Audio/video detected for $label."
}

echo "=== E2E RTMP Proxy Origin Cluster Test ==="
echo "Workspace: $WORKSPACE"
echo ""

# --- Pre-checks ---
if [[ ! -f "$SOURCE_FLV" ]]; then
  echo "Error: test source not found: $SOURCE_FLV" >&2
  exit 1
fi
if ! command -v ffmpeg &>/dev/null; then
  echo "Error: ffmpeg not found in PATH" >&2
  exit 1
fi
if ! command -v ffprobe &>/dev/null; then
  echo "Error: ffprobe not found in PATH" >&2
  exit 1
fi
if ! command -v curl &>/dev/null; then
  echo "Error: curl not found in PATH" >&2
  exit 1
fi

# --- Step 0: Clean up stale state ---
# Remove stale SRS PID files that prevent restart.
rm -f "$WORKSPACE/trunk/objs/origin1.pid" "$WORKSPACE/trunk/objs/origin2.pid"
# Kill any leftover processes on our ports (proxy + origins).
ALL_PORTS="$PROXY_RTMP_PORT $PROXY_HTTP_API_PORT $PROXY_HTTP_SERVER_PORT $PROXY_WEBRTC_PORT $PROXY_SRT_PORT $PROXY_SYSTEM_API_PORT"
ALL_PORTS="$ALL_PORTS $ORIGIN1_RTMP_PORT $ORIGIN1_HTTP_PORT $ORIGIN1_API_PORT $ORIGIN1_RTC_PORT $ORIGIN1_SRT_PORT"
ALL_PORTS="$ALL_PORTS $ORIGIN2_RTMP_PORT $ORIGIN2_HTTP_PORT $ORIGIN2_API_PORT $ORIGIN2_RTC_PORT $ORIGIN2_SRT_PORT"
for port in $ALL_PORTS; do
  lsof -ti :"$port" 2>/dev/null | xargs kill 2>/dev/null || true
done
sleep 1

# --- Step 1: Build proxy ---
echo "=== Step 1: Building proxy ==="
cd "$WORKSPACE"
make -s 2>&1
echo "Proxy built: $WORKSPACE/bin/srs-proxy"

# --- Step 2: Build SRS origins (if not already built) ---
if [[ ! -f "$SRS_BINARY" ]]; then
  echo "=== Step 2: Building SRS origins ==="
  cd "$WORKSPACE/trunk"
  ./configure && make 2>&1 | tail -3
  echo "SRS origins built: $SRS_BINARY"
else
  echo "=== Step 2: SRS origins already built ==="
fi

# --- Step 3: Start proxy ---
echo "=== Step 3: Starting proxy (RTMP :$PROXY_RTMP_PORT, System API :$PROXY_SYSTEM_API_PORT) ==="
cd "$WORKSPACE"
env PROXY_RTMP_SERVER=$PROXY_RTMP_PORT \
    PROXY_HTTP_API=$PROXY_HTTP_API_PORT \
    PROXY_HTTP_SERVER=$PROXY_HTTP_SERVER_PORT \
    PROXY_WEBRTC_SERVER=$PROXY_WEBRTC_PORT \
    PROXY_SRT_SERVER=$PROXY_SRT_PORT \
    PROXY_SYSTEM_API=$PROXY_SYSTEM_API_PORT \
    PROXY_LOAD_BALANCER_TYPE=memory \
    ./bin/srs-proxy >/tmp/srs-proxy-cluster-e2e.log 2>&1 &
PROXY_PID=$!
echo "Proxy PID: $PROXY_PID"

wait_for_http "http://127.0.0.1:$PROXY_SYSTEM_API_PORT/api/v1/versions" "Proxy System API"

if ! kill -0 "$PROXY_PID" 2>/dev/null; then
  echo "Error: proxy failed to start. Logs:" >&2
  cat /tmp/srs-proxy-cluster-e2e.log >&2
  exit 1
fi
echo "Proxy started."

# --- Step 4: Start two SRS origins ---
echo "=== Step 4: Starting two SRS origins ==="
ulimit -n 10000 2>/dev/null || true
cd "$WORKSPACE/trunk"

./objs/srs -c conf/origin1-for-proxy.conf >/tmp/srs-origin1-cluster-e2e.log 2>&1 &
ORIGIN_PIDS+=($!)
echo "SRS origin1 PID: ${ORIGIN_PIDS[0]}"

./objs/srs -c conf/origin2-for-proxy.conf >/tmp/srs-origin2-cluster-e2e.log 2>&1 &
ORIGIN_PIDS+=($!)
echo "SRS origin2 PID: ${ORIGIN_PIDS[1]}"

wait_for_http "http://127.0.0.1:$ORIGIN1_API_PORT/api/v1/versions" "SRS origin1 HTTP API"
wait_for_http "http://127.0.0.1:$ORIGIN2_API_PORT/api/v1/versions" "SRS origin2 HTTP API"

# Wait for both SRS origins to register with proxy (heartbeat interval is 9s).
echo "Waiting for both SRS origins to register with proxy (up to 20s)..."
for i in $(seq 1 20); do
  registered=$(grep -c "Register SRS media server" /tmp/srs-proxy-cluster-e2e.log 2>/dev/null || true)
  if [[ $registered -ge 2 ]]; then
    echo "Both origins registered."
    break
  fi
  sleep 1
done

registered=$(grep -c "Register SRS media server" /tmp/srs-proxy-cluster-e2e.log 2>/dev/null || true)
if [[ $registered -lt 2 ]]; then
  echo "Error: expected two origin registrations, got $registered. Proxy logs:" >&2
  cat /tmp/srs-proxy-cluster-e2e.log >&2
  exit 1
fi

for pid in "${ORIGIN_PIDS[@]}"; do
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "Error: SRS origin failed to start. Origin1 logs:" >&2
    cat /tmp/srs-origin1-cluster-e2e.log >&2
    echo "Origin2 logs:" >&2
    cat /tmp/srs-origin2-cluster-e2e.log >&2
    exit 1
  fi
done
echo "Two SRS origins started and registered."

# --- Step 5: Publish RTMP streams until both origins own at least one stream ---
echo "=== Step 5: Publishing multiple RTMP streams to proxy ==="
STREAM_PREFIX="cluster$(date +%s)"
STREAMS=()
STREAM_ORIGINS=()
origin1_count=0
origin2_count=0

# The memory load balancer picks a random healthy origin for each new stream and
# keeps that stream sticky. Publish several unique streams to verify distribution
# while keeping the test extremely unlikely to fail from random selection alone.
for i in $(seq 1 20); do
  stream="${STREAM_PREFIX}_$i"
  STREAMS+=("$stream")

  ffmpeg -stream_loop -1 -re -i "$SOURCE_FLV" -c copy -f flv \
    "rtmp://localhost:$PROXY_RTMP_PORT/live/$stream" >/tmp/srs-ffmpeg-cluster-e2e-$i.log 2>&1 &
  FFMPEG_PIDS+=($!)
  echo "Started publisher for live/$stream, PID: ${FFMPEG_PIDS[$((${#FFMPEG_PIDS[@]} - 1))]}"

  sleep 3

  if ! kill -0 "${FFMPEG_PIDS[$((${#FFMPEG_PIDS[@]} - 1))]}" 2>/dev/null; then
    echo "Error: FFmpeg publisher failed for $stream. Logs:" >&2
    cat "/tmp/srs-ffmpeg-cluster-e2e-$i.log" >&2
    exit 1
  fi

  owner=$(detect_origin_for_stream "$stream")
  STREAM_ORIGINS+=("$owner")
  echo "Stream live/$stream is owned by $owner."

  if [[ "$owner" == "origin1" ]]; then
    origin1_count=$((origin1_count + 1))
  else
    origin2_count=$((origin2_count + 1))
  fi

  if [[ $origin1_count -gt 0 && $origin2_count -gt 0 ]]; then
    break
  fi
done

if [[ $origin1_count -eq 0 || $origin2_count -eq 0 ]]; then
  echo "FAIL: streams were not distributed to both origins." >&2
  echo "origin1_count=$origin1_count, origin2_count=$origin2_count" >&2
  exit 1
fi

echo "PASS: stream distribution detected: origin1=$origin1_count, origin2=$origin2_count."

# --- Step 6: Verify playback via proxy and direct owning origins ---
echo "=== Step 6: Verifying playback and sticky origin ownership ==="
for i in "${!STREAMS[@]}"; do
  stream="${STREAMS[$i]}"
  owner="${STREAM_ORIGINS[$i]}"

  # Verify proxy playback works for every published stream.
  verify_probe_has_av "rtmp://localhost:$PROXY_RTMP_PORT/live/$stream" "proxy live/$stream"

  # Verify the stream is on exactly one origin, and verify direct playback from
  # the owning origin to prove the proxy really published to that backend.
  current_owner=$(detect_origin_for_stream "$stream")
  if [[ "$current_owner" != "$owner" ]]; then
    echo "FAIL: stream live/$stream moved from $owner to $current_owner; expected sticky routing." >&2
    exit 1
  fi

  if [[ "$owner" == "origin1" ]]; then
    verify_probe_has_av "rtmp://localhost:$ORIGIN1_RTMP_PORT/live/$stream" "origin1 live/$stream"
  else
    verify_probe_has_av "rtmp://localhost:$ORIGIN2_RTMP_PORT/live/$stream" "origin2 live/$stream"
  fi
done

echo ""
echo "=== E2E RTMP Proxy Origin Cluster Test PASSED ==="
