#!/bin/bash
# Check that a trimmed openssl-4-fit builds with and without assembly and holds only what those builds read,
# from a run.sh of the trimmed tree:
#   - every target configured, built, installed and passed verify.pl, with assembly and with -no-asm, so the
#     tree still has every assembly file and perlasm script a build with assembly reads;
#   - the builds with assembly read assembly, and the -no-asm builds read none;
#   - no file is left out of the keep list, and the tree holds exactly the keep list.
# Usage: check.sh <openssl-4-fit tree> <run.sh work-dir>
set -u
TREE=${1:?usage: check.sh <tree> <work-dir>}; WORK=${2:?usage: check.sh <tree> <work-dir>}
FAILED=0
STATUS=$WORK/report/status.txt
if [[ ! -f $STATUS || ! -f $WORK/report/drop.txt ]]; then
    echo "no $STATUS or $WORK/report/drop.txt, run run.sh first"
    echo "check failed"; exit 1
fi

BAD=$(grep -E '^(asm|no-asm) ' "$STATUS" | grep -v ' ok asm=')
[[ -z $BAD ]] || { echo "targets that did not build:"; echo "$BAD"; FAILED=1; }
grep -qx 'failed=0' "$STATUS" || { echo "run.sh did not pass: $(tail -1 "$STATUS")"; FAILED=1; }
NA=$(grep -c '^asm .* ok ' "$STATUS"); NN=$(grep -c '^no-asm .* ok ' "$STATUS")
echo "targets built: $NA with assembly, $NN with -no-asm"
[[ $NA -gt 0 && $NA == "$NN" ]] || { echo "the two modes did not build the same targets"; FAILED=1; }
ASM_READ=$(grep '^asm .* ok ' "$STATUS" | sed 's/.*asm=//' | awk '{s += $1} END {print s + 0}')
NOASM_READ=$(grep '^no-asm ' "$STATUS" | sed 's/.*asm=//' | awk '{s += $1} END {print s + 0}')
echo "assembly and perlasm files read: $ASM_READ with assembly, $NOASM_READ with -no-asm"
[[ $ASM_READ -gt 0 ]] || { echo "the builds with assembly read none"; FAILED=1; }
[[ $NOASM_READ == 0 ]] || { echo "the -no-asm builds read assembly"; FAILED=1; }

DROP=$(wc -l < "$WORK/report/drop.txt" | tr -d ' ')
echo "files no build reads: $DROP"
head -20 "$WORK/report/drop.txt"
[[ $DROP == 0 ]] || FAILED=1
FILES=$(cd "$TREE" && find . -type f | wc -l | tr -d ' ')
KEEP=$(wc -l < "$WORK/report/keep.txt" | tr -d ' ')
echo "files in the tree: $FILES, in the keep list: $KEEP"
[[ $FILES == "$KEEP" ]] || FAILED=1
[[ $FAILED == 0 ]] && echo "check passed" || echo "check failed"
exit $FAILED
