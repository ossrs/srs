#!/bin/bash
# Usage: perf-source.sh [-bufsize <size>] <out.flv>
#
# Encodes the flat CBR source every perf measurement uses, then checks it and
# writes <out>-flatness.json beside it. It exits 1 when the source is not flat.
#
# 60s of testsrc2 768x320 at 25 fps and a 440 Hz sine; H.264 main at 200 kbps
# CBR with no B frames and an IDR every 2s; AAC 32 kbps stereo. The one-frame
# buffer (8k) keeps every frame the same size; a larger one lets seconds drift.
set -euo pipefail

bufsize=8k
if [[ "${1:-}" == -bufsize ]]; then bufsize="$2"; shift 2; fi
out="${1:?usage: perf-source.sh [-bufsize <size>] <out.flv>}"
target=200

mkdir -p "$(dirname "$out")"
ffmpeg -v error -y \
    -f lavfi -i testsrc2=size=768x320:rate=25 \
    -f lavfi -i sine=frequency=440:sample_rate=44100 -t 60 \
    -c:v libx264 -profile:v main -preset veryfast -bf 0 \
    -b:v "${target}k" -minrate "${target}k" -maxrate "${target}k" -bufsize "$bufsize" \
    -x264-params nal-hrd=cbr:force-cfr=1:keyint=50:min-keyint=50:scenecut=0 \
    -c:a aac -b:a 32k -ac 2 "$out"

flatness="${out%.flv}-flatness.json"
if bash "$(dirname "$0")/perf-source-check.sh" "$out" "$target" > "$flatness"; then
    cat "$flatness"
else
    cat "$flatness" >&2
    exit 1
fi
