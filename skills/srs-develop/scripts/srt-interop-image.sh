#!/bin/bash
#
# Build the SRT interop image: libsrt's own srt-live-transmit and srt-test-live, on Haivision libsrt v1.5.7 with
# bonding and encryption, from tools/srt-interop/Dockerfile. The srs-integ SRT tests run them as outside peers of
# the other SRT libraries and of SRS, and skip them when the image is missing.
#
# Usage:
#   bash skills/srs-develop/scripts/srt-interop-image.sh
#
# Output:
#   the Docker image srs-srt-interop:1.5.7, or $SRT_INTEROP_IMAGE, the name the tests look for
#
# Docker caches the build, so a later run takes a second while the Dockerfile is the same. Remove the image with
# docker rmi to start over.
#
# Needs Docker.
#
# Exit code is 0 when the image is built and runs both tools, 1 otherwise.
set -e

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
IMAGE="${SRT_INTEROP_IMAGE:-srs-srt-interop:1.5.7}"

if ! command -v docker >/dev/null 2>&1; then
  echo "Error: docker not found in PATH." >&2
  exit 1
fi

docker build -t "$IMAGE" "$ROOT/tools/srt-interop"

for tool in srt-live-transmit srt-test-live; do
  if ! docker run --rm "$IMAGE" sh -c "command -v $tool" >/dev/null; then
    echo "Error: $tool is not in $IMAGE." >&2
    exit 1
  fi
done
echo "Built $IMAGE with srt-live-transmit and srt-test-live"
