#!/bin/bash
#
# Group the sanitizer reports of a srs-utest-asan-linux.sh log by the server code that allocated or touched the
# memory, so one class or function of the memory audit can find its own reports.
#
# Usage:
#   bash skills/srs-develop/scripts/cpp-memory-asan-frames.sh [--direct] <log> [name]
#
#   --direct    Leaks: only direct leaks. An indirect leak is memory reachable from a direct one.
#   log         The report of srs-utest-asan-linux.sh.
#   name        Show only the reports whose server frame is in this class or function, such as SrsFormat or
#               srs_av_base64_decode; a class matches its methods too. Default: all.
#
# Output, one line per server frame, most reports first:
#   count kind frame file:line tests
# where frame is the first stack frame in trunk/src outside utest/ (for a leak, the allocating function), kind is
# direct, indirect or the AddressSanitizer error, and tests are up to three unit tests whose stacks reach it.

DIRECT=no
if [[ $1 == --direct ]]; then
    DIRECT=yes
    shift
fi
LOG=$1
NAME=$2
if [[ ! -f $LOG ]]; then
    echo "usage: $0 [--direct] <log> [name]"
    exit 1
fi

LC_ALL=C awk -v direct=$DIRECT -v name="$NAME" '
function flush(    key) {
    if (kind == "" || frame == "") return
    if (direct == "yes" && kind == "indirect") return
    if (name != "" && !matches(fname)) return
    key = kind " " frame
    count[key]++
    n = split(tests, t, " ")
    for (i = 1; i <= n; i++) if (t[i] != "" && !((key, t[i]) in seen)) {
        seen[key, t[i]] = 1
        ntests[key]++
        if (ntests[key] <= 3) shown[key] = shown[key] (shown[key] == "" ? "" : ",") t[i]
    }
}
function matches(f,    p) {
    p = "(^|[ :<])" name "(::|<|$)"
    return f ~ p
}
/^(Direct|Indirect) leak of/ || /^==[0-9]+==ERROR: AddressSanitizer: / {
    flush()
    if ($0 ~ /^Direct/) kind = "direct"
    else if ($0 ~ /^Indirect/) kind = "indirect"
    else { kind = $0; sub(/^==[0-9]+==ERROR: AddressSanitizer: /, "", kind); sub(/ .*/, "", kind) }
    frame = ""; fname = ""; tests = ""
    next
}
/^[ \t]+#[0-9]+ 0x[0-9a-f]+ in .* (\.\.\/)*src\/[^ ]+$/ {
    if (kind == "") next
    where = $NF; sub(/^(\.\.\/)*/, "", where)
    f = $0; sub(/^[ \t]+#[0-9]+ 0x[0-9a-f]+ in /, "", f); sub(/ [^ ]+$/, "", f)
    if (match(f, /[A-Za-z0-9_]+_Test::TestBody/)) tests = tests " " substr(f, RSTART, RLENGTH - 15)
    if (frame == "" && where !~ /\/utest\//) {
        fname = f; sub(/\(.*$/, "", fname)
        frame = fname " " where
    }
}
END {
    flush()
    for (key in count) {
        more = (ntests[key] > 3) ? " +" (ntests[key] - 3) : ""
        print count[key] " " key " " shown[key] more
    }
}
' "$LOG" | sort -k1,1nr -k3,3
