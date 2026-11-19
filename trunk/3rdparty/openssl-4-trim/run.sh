#!/bin/bash
# The inventory of openssl-4-fit: build it with the --ssl-4=on options of trunk/auto/depends.sh for every
# target, twice: with assembly, as depends.sh builds it, and with -no-asm, which depends.sh adds on Loongson
# and with --nasm=off. Then list the files of the tree no build reads. On macOS it builds darwin64-arm64 and
# darwin64-x86_64 here; with Docker it builds the Linux targets of linux-all.sh in a container.
# Windows builds SRS with vcpkg's OpenSSL, not this tree, so it is not a target.
#
# Usage: run.sh <work-dir> [<tree>], from any directory; <work-dir> must be outside the tree, and <tree> is
# trunk/3rdparty/openssl-4-fit unless given, such as a copy of an older, untrimmed tree.
# Output: <work-dir>/report/report.txt, keep.txt, drop.txt and status.txt (one line a target and mode, read by
# check.sh); per target <work-dir>/<mode>/{mac,linux}/<name>/, <mode> asm or no-asm.
set -u
WORK=${1:?usage: run.sh <work-dir> [<tree>]}
HERE=$(cd "$(dirname "$0")" && pwd)
TRUNK=$(cd "$HERE/../.." && pwd)
TREE=$(cd "${2:-$TRUNK/3rdparty/openssl-4-fit}" && pwd) || exit 1
IMAGE=${IMAGE:-srs-openssl-trim:local}
BASE=$(bash "$HERE/options.sh" "$TRUNK/auto/depends.sh") || { echo "no options in depends.sh"; exit 1; }
mkdir -p "$WORK/report" || exit 1
WORK=$(cd "$WORK" && pwd)
STATUS=$WORK/report/status.txt
: > "$STATUS"
echo "tree: $TREE"
echo "options: $BASE"
FAILED=0
DIRS=()

# A target's line in status.txt: ok when it configured, built, installed and passed verify.pl, with the
# number of assembly and perlasm files it read.
status() {
    local mode=$1 dir=$2 ok=$3 asm=0
    [[ -s $dir/read.txt ]] && asm=$(grep -cE '(^|/)asm/|^crypto/perlasm/|^crypto/[^/]*cpuid\.pl$|\.(S|s|asm)$' "$dir/read.txt")
    echo "$mode $(basename "$(dirname "$dir")")/$(basename "$dir") $ok asm=$asm" >> "$STATUS"
    [[ $ok == ok ]] && DIRS+=("$dir")
}

if docker version >/dev/null 2>&1; then
    docker build -q -t "$IMAGE" "$HERE" >/dev/null || { echo "docker build failed"; exit 1; }
    HAS_DOCKER=1
else
    echo "no Docker: the Linux targets are not built"; HAS_DOCKER=0; FAILED=1
fi

for MODE in asm no-asm; do
    OPTIONS=$BASE
    [[ $MODE == no-asm ]] && OPTIONS="$BASE -no-asm"
    export OPTIONS
    echo "== $MODE: $OPTIONS"
    if [[ $(uname -s) == Darwin ]]; then
        M=$WORK/$MODE/mac
        mkdir -p "$M"; : > "$M/summary.txt"
        # ./config picks the host's arch, as depends.sh runs it; the other arch is configured by name.
        if [[ $(uname -m) == arm64 ]]; then OTHER=darwin64-x86_64; else OTHER=darwin64-arm64; fi
        for t in darwin-native $OTHER; do
            if [[ $t == darwin-native ]]; then set -- ./config; else set -- ./Configure $t-cc; fi
            OK=ok
            bash "$HERE/inventory.sh" "$TREE" "$M" $t cc "$@" 2>&1 | tee -a "$M/summary.txt"
            [[ ${PIPESTATUS[0]} -eq 0 ]] || OK=failed
            if [[ $OK == ok ]]; then
                perl "$HERE/verify.pl" "$M/$t" 2>&1 | sed "s|$M/||" | tee -a "$M/summary.txt"
                [[ ${PIPESTATUS[0]} -eq 0 ]] || OK=failed
            fi
            [[ $OK == ok ]] || FAILED=1
            rm -rf "$M/$t/tree" "$M/$t/prefix"
            status $MODE "$M/$t" $OK
        done
    fi
    if [[ $HAS_DOCKER == 1 ]]; then
        L=$WORK/$MODE/linux
        mkdir -p "$L"
        docker run --rm -e OPTIONS="$OPTIONS" -e JOBS="${JOBS:-8}" -v "$TREE":/src:ro -v "$HERE":/trim:ro \
            -v "$L":/out "$IMAGE" bash /trim/linux-all.sh > "$WORK/$MODE/linux-run.log" 2>&1
        r=$?; echo "docker exit $r"; [[ $r -eq 0 ]] || FAILED=1
        cat "$L/summary.txt"
        # linux-all.sh writes "<name>: configure ..., read N of M files" for a target that built, and verify.pl
        # "<name>: ... 0 missing ...; ... 0 missing" for one whose cross-check passed.
        for d in "$L"/*/; do
            d=${d%/}; n=$(basename "$d"); OK=failed
            grep -q "^$n: configure .* read [0-9]* of" "$L/summary.txt" && \
                grep -q "^$n: .* 0 missing from read.txt; .* 0 missing$" "$L/summary.txt" && OK=ok
            [[ $OK == ok ]] || FAILED=1
            status $MODE "$d" $OK
        done
    fi
done

if [[ ${#DIRS[@]} -gt 0 ]]; then
    perl "$HERE/report.pl" "$TREE" "$WORK/report" "${DIRS[@]}" > "$WORK/report/report.txt" || FAILED=1
    cat "$WORK/report/report.txt"
fi
cat "$STATUS"
for d in "${DIRS[@]}"; do
    [[ -s $d/libs.txt ]] && echo "${d#$WORK/}: $(awk '{printf "%s ", $1}' "$d/libs.txt")"
done
echo "failed=$FAILED" >> "$STATUS"
echo "DONE failed=$FAILED"
exit $FAILED
