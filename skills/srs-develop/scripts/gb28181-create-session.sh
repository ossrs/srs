#!/bin/bash
# Create an external-SIP GB28181 session through the SRS publish API without
# opening the returned media TCP endpoint.
set -euo pipefail

API_URL="http://127.0.0.1:1985"
STREAM_ID="gb-create-session-$$"
SSRC="47190001"
EXPECT_CODE="0"

usage() {
  cat <<EOF
Usage: $0 [options]

Create a GB28181 session through the external-SIP publish API without opening
the media TCP connection. The raw JSON API response is written to stdout.

Options:
  --api-url URL         SRS HTTP API base URL (default: $API_URL)
  --id ID               GB stream ID (default: $STREAM_ID)
  --ssrc SSRC           RTP SSRC in decimal (default: $SSRC)
  --expect-code CODE    Required API response code (default: $EXPECT_CODE)
  -h, --help            Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --api-url)
      API_URL="$2"
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
    --expect-code)
      EXPECT_CODE="$2"
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

for tool in curl python3; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Error: $tool is required" >&2
    exit 1
  fi
done

REQUEST_BODY=$(python3 - "$STREAM_ID" "$SSRC" <<'PY'
import json
import sys

print(json.dumps({"id": sys.argv[1], "ssrc": sys.argv[2]}))
PY
)

API_URL="${API_URL%/}"
echo "Create GB session through publish API: id=$STREAM_ID, ssrc=$SSRC" >&2
PUBLISH_RESPONSE=$(curl --silent --show-error \
  --request POST \
  --header 'Content-Type: application/json' \
  --data "$REQUEST_BODY" \
  "$API_URL/gb/v1/publish/")

# Always expose the response so callers can inspect or parse the endpoint.
printf '%s\n' "$PUBLISH_RESPONSE"

RESPONSE_CODE=$(printf '%s' "$PUBLISH_RESPONSE" | python3 -c '
import json
import sys

print(json.load(sys.stdin).get("code", -1))
')
if [[ "$RESPONSE_CODE" != "$EXPECT_CODE" ]]; then
  echo "FAIL: expected publish API code $EXPECT_CODE, got $RESPONSE_CODE" >&2
  exit 1
fi
