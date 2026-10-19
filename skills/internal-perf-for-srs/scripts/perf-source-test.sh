#!/bin/bash
# Checks the flat CBR source in srs-perf:local: the image carries it with a flat
# flatness record, and the check rejects a VBR file and a CBR encode with a
# large buffer.
set -euo pipefail

cd "$(dirname "$0")/../../.."
scripts="$PWD/skills/internal-perf-for-srs/scripts"
failed=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; failed=1; }

in_image() {
    docker run --rm -v "$scripts:/perf-scripts:ro" -v "$PWD/doc:/doc:ro" srs-perf:local "$@"
}

# The source layer comes before the SRS source, so an SRS change keeps it cached.
source_line=$(grep -n 'perf-source.sh /srs/perf/cbr.flv' cmd/perf/Dockerfile | cut -d: -f1 || true)
trunk_line=$(grep -n '^COPY trunk/' cmd/perf/Dockerfile | cut -d: -f1 || true)
if [[ -n "$source_line" && -n "$trunk_line" && "$source_line" -lt "$trunk_line" ]]; then
    pass "the Dockerfile makes the source before it copies trunk/"
else
    fail "the Dockerfile makes the source before it copies trunk/"
fi

if in_image test -s /srs/perf/cbr.flv; then
    pass "the image has /srs/perf/cbr.flv"
else
    fail "the image has /srs/perf/cbr.flv"
fi

record=$(in_image cat /srs/perf/cbr-flatness.json 2>/dev/null || true)
if jq -e '.flat == true and .target_kbps == 200
        and .second.min >= 194 and .second.max <= 206 and .second.count >= 58
        and .frame.sd <= 0.03 * .frame.mean and .frame.count >= 1450
        and .first_second_kbps > 0 and .first_frame_kbit > 0
        and (.ffmpeg | test("^ffmpeg version"))' <<<"$record" >/dev/null 2>&1; then
    pass "the image's flatness record is flat with every field"
else
    fail "the image's flatness record is flat with every field: $record"
fi

if in_image bash /perf-scripts/perf-source-check.sh /srs/perf/cbr.flv 200 >/dev/null; then
    pass "the check passes the image's source"
else
    fail "the check passes the image's source"
fi

vbr=$(in_image bash /perf-scripts/perf-source-check.sh /doc/source.flv 200 || true)
if jq -e '.flat == false and (.reason | length > 0)' <<<"$vbr" >/dev/null 2>&1; then
    pass "the check fails doc/source.flv (VBR)"
else
    fail "the check fails doc/source.flv (VBR): $vbr"
fi

if in_image bash /perf-scripts/perf-source.sh -bufsize 200k /tmp/cbr-200k.flv >/dev/null 2>&1; then
    fail "the check fails a 200k-buffer CBR encode"
else
    pass "the check fails a 200k-buffer CBR encode"
fi

if in_image bash /perf-scripts/perf-source-check.sh /srs/perf/missing.flv 200 >/dev/null 2>&1; then
    fail "the check fails a missing file"
else
    pass "the check fails a missing file"
fi

[[ "$failed" == 0 ]] && echo "perf-source-test: all passed" || { echo "perf-source-test: failed"; exit 1; }
