#!/bin/bash
# Split a change into the files the audit's two phases read, so the auditor
# sees the code before the commit message and the tests:
#   code.diff   the change without its tests
#   stat.txt    the code files changed
#   base.txt    the commit before the change
#   tests.diff  the test files changed
#   message.txt the commit messages
#   tip.txt     the audited commit
# Usage: audit-diffs.sh <commit|base..tip> <out-dir>
set -e

range=$1
out=$2
if [ -z "$range" ] || [ -z "$out" ]; then
    echo "usage: $0 <commit|base..tip> <out-dir>" >&2
    exit 1
fi

case $range in
    *..*)
        base=${range%%..*}
        tip=${range##*..}
        ;;
    *)
        base="$range^"
        tip=$range
        ;;
esac
base=$(git rev-parse --verify "$base")
tip=$(git rev-parse --verify "$tip")

tests=(trunk/src/utest cmd/integ trunk/3rdparty/srs-bench '*_test.go' '*test*.sh')
excludes=()
for t in "${tests[@]}"; do
    excludes+=(":(exclude)$t")
done

mkdir -p "$out"
git diff "$base" "$tip" -- . "${excludes[@]}" > "$out/code.diff"
git diff --stat=120 "$base" "$tip" -- . "${excludes[@]}" > "$out/stat.txt"
echo "$base" > "$out/base.txt"
git diff "$base" "$tip" -- "${tests[@]}" > "$out/tests.diff"
git log --format='%H%n%s%n%n%b' "$base..$tip" > "$out/message.txt"
echo "$tip" > "$out/tip.txt"

echo "base $base tip $tip"
wc -l "$out/code.diff" "$out/tests.diff" | sed '$d'
