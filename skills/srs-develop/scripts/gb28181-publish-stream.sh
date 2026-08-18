#!/bin/bash
# Simulate an external-SIP GB28181 publisher. It creates an SRS session through
# the publish API, sends RFC4571-framed RTP/PS over TCP, then disconnects.
set -euo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
GB_CREATE_SESSION="$SCRIPT_DIR/gb28181-create-session.sh"
API_URL="http://127.0.0.1:1985"
MEDIA_HOST="127.0.0.1"
MEDIA_PORT=""
STREAM_ID="gb-publish-stream-$$"
SSRC="47190001"
HOLD_SECONDS="0.2"

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --api-url URL         SRS HTTP API base URL (default: $API_URL)
  --media-host HOST     GB media TCP host (default: $MEDIA_HOST)
  --media-port PORT     GB media TCP port (default: publish API response)
  --id ID               GB stream ID (default: $STREAM_ID)
  --ssrc SSRC           RTP SSRC in decimal (default: $SSRC)
  --hold SECONDS        Hold TCP open after sending (default: $HOLD_SECONDS)
  -h, --help            Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --api-url)
      API_URL="$2"
      shift 2
      ;;
    --media-host)
      MEDIA_HOST="$2"
      shift 2
      ;;
    --media-port)
      MEDIA_PORT="$2"
      shift 2
      ;;
    --id)
      STREAM_ID="$2"
      shift 2
      ;;
    --ssrc)
      SSRC="$2"
      shift 2
      ;;
    --hold)
      HOLD_SECONDS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if ! command -v python3 >/dev/null 2>&1; then
  echo "Error: python3 is required" >&2
  exit 1
fi
if [[ ! -x "$GB_CREATE_SESSION" ]]; then
  echo "Error: GB session creation helper is not executable: $GB_CREATE_SESSION" >&2
  exit 1
fi

PUBLISH_RESPONSE=$("$GB_CREATE_SESSION" \
  --api-url "$API_URL" \
  --id "$STREAM_ID" \
  --ssrc "$SSRC")
echo "Publish response: $PUBLISH_RESPONSE"

read -r RESPONSE_CODE RESPONSE_PORT RESPONSE_IS_TCP <<EOF
$(printf '%s' "$PUBLISH_RESPONSE" | python3 -c '
import json
import sys

response = json.load(sys.stdin)
print(response.get("code", -1), response.get("port", ""), str(response.get("is_tcp", False)).lower())
')
EOF

if [[ "$RESPONSE_CODE" != "0" ]]; then
  echo "FAIL: publish API returned code $RESPONSE_CODE" >&2
  exit 1
fi
if [[ "$RESPONSE_IS_TCP" != "true" ]]; then
  echo "FAIL: publish API did not return a TCP media endpoint" >&2
  exit 1
fi
if [[ -z "$MEDIA_PORT" ]]; then
  MEDIA_PORT="$RESPONSE_PORT"
fi

echo "Connect RTP/PS over TCP: $MEDIA_HOST:$MEDIA_PORT"
python3 - "$MEDIA_HOST" "$MEDIA_PORT" "$SSRC" "$HOLD_SECONDS" <<'PY'
import socket
import struct
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
ssrc = int(sys.argv[3])
hold_seconds = float(sys.argv[4])

if not 0 < ssrc <= 0xFFFFFFFF:
    raise SystemExit("SSRC must be between 1 and 4294967295")

# A PS pack and complete audio PES packet derived from the SRS GB28181 parser
# regression fixture. The PES payload contains two valid AAC-LC/44.1kHz/stereo
# ADTS silence frames, rather than placeholder bytes that trigger an ADTS
# warning in the GB muxer. Sending two PS packs makes SRS deliver the first one
# to the GB session and marks the media transport connected.
ps_header = bytes.fromhex("000001ba44686e4c9401013013feffff0000a005")
aac_adts = bytes.fromhex(
    "fff1508003dffcde02004c61766336322e32382e31303100422008c11838"
    "fff1508001bffc211004608c1c"
)
pes_optional_header = bytes.fromhex("8c8009211a1ba351fffffff8")
pes_packet_length = len(pes_optional_header) + len(aac_adts)
audio_pes = (
    bytes.fromhex("000001c0")
    + struct.pack("!H", pes_packet_length)
    + pes_optional_header
    + aac_adts
)
ps_pack = ps_header + audio_pes

with socket.create_connection((host, port), timeout=3) as conn:
    for sequence in (1, 2):
        rtp = struct.pack("!BBHII", 0x80, 0xE0, sequence, sequence * 90000, ssrc) + ps_pack
        conn.sendall(struct.pack("!H", len(rtp)) + rtp)
        time.sleep(0.05)
    time.sleep(hold_seconds)

print("GB TCP publisher disconnected")
PY
