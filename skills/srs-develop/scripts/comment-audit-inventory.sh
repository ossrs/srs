#!/bin/bash
#
# List the units of the comment audit, one per line, in the order the audit runs them.
#
# Usage:
#   bash skills/srs-develop/scripts/comment-audit-inventory.sh [oryx-dir]
#
#   oryx-dir    The Oryx checkout to list, default ../oryx next to this SRS checkout.
#
# Output, one unit per line:
#   repo comment-lines file...
# where repo is srs or oryx, comment-lines counts the lines that start with // or /* or *, and the files are
# relative to that repository.
#
# Units:
#   - C++ server: one .hpp/.cpp pair per unit, in trunk/src/{core,kernel,protocol,app,main}. Not utest, not
#     3rdparty, and not the llhttp parser copied from nodejs/llhttp (srs_protocol_http_stack_llhttp*).
#   - Go proxy: one package per unit in cmd/proxy and internal/, without *_test.go. Packages with fewer than
#     20 comment lines are joined into one unit at the end.
#   - Oryx Go: one file per unit in platform/ and releases/, without *_test.go and vendor/. Files with fewer
#     than 20 comment lines are joined into one unit at the end.
#   - Oryx UI: one unit with every ui/src .js file, without tests and setupTests.js.

SMALL=20

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
ORYX=${1:-$ROOT/../oryx}
ORYX=$(cd "$ORYX" && pwd) || exit 1

count() {
    (cd "$1" && shift && cat "$@" | grep -cE '^[[:space:]]*(//|/\*|\*)')
}

cd "$ROOT" || exit 1
for d in core kernel protocol app main; do
    for base in $(ls trunk/src/$d | grep -E '\.(hpp|cpp)$' | sed -E 's/\.(hpp|cpp)$//' | LC_ALL=C sort -u); do
        case $base in srs_protocol_http_stack_llhttp*) continue ;; esac
        files=()
        for ext in hpp cpp; do
            [[ -f trunk/src/$d/$base.$ext ]] && files+=("trunk/src/$d/$base.$ext")
        done
        echo "srs $(count "$ROOT" "${files[@]}") ${files[*]}"
    done
done

small=()
for d in cmd/proxy $(ls -d internal/*/ | sed 's|/$||' | LC_ALL=C sort); do
    files=($(find "$d" -maxdepth 1 -name '*.go' ! -name '*_test.go' | LC_ALL=C sort))
    [[ ${#files[@]} -eq 0 ]] && continue
    n=$(count "$ROOT" "${files[@]}")
    if [[ $n -lt $SMALL ]]; then small+=("${files[@]}"); else echo "srs $n ${files[*]}"; fi
done
[[ ${#small[@]} -gt 0 ]] && echo "srs $(count "$ROOT" "${small[@]}") ${small[*]}"

cd "$ORYX" || exit 1
small=()
for f in $(find platform releases -maxdepth 1 -name '*.go' ! -name '*_test.go' | LC_ALL=C sort); do
    n=$(count "$ORYX" "$f")
    if [[ $n -lt $SMALL ]]; then small+=("$f"); else echo "oryx $n $f"; fi
done
[[ ${#small[@]} -gt 0 ]] && echo "oryx $(count "$ORYX" "${small[@]}") ${small[*]}"

files=($(find ui/src -name '*.js' ! -name '*.test.js' ! -name 'setupTests.js' | LC_ALL=C sort))
echo "oryx $(count "$ORYX" "${files[@]}") ${files[*]}"
