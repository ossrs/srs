#!/bin/bash
# List the OpenSSL APIs that SRS uses and OpenSSL 3 deprecates or removes, by compiling every SRS
# source against the OpenSSL 3 headers instead of the bundled OpenSSL 1.1, syntax only.
#
#   openssl3-api-check.sh [<OpenSSL 3 include dir>]
#
# Run it in a configured SRS trunk, one with objs/Makefile; it compiles each ./objs/src/*.o source
# with the same command as the Makefile, the bundled -I./objs/openssl/include replaced by the given
# directory, and writes no object file; the compiler output of each source goes to
# objs/openssl3-api-check/. The directory defaults to SRS_OPENSSL3_INCLUDE, then to
# Homebrew's openssl@3, then to /usr/include.
#
# It compiles each source twice:
#   - as is, and prints each use the compiler warns is deprecated:
#       DEPRECATED <file>:<line> <name>
#   - with OPENSSL_NO_DEPRECATED, as an OpenSSL built without deprecated APIs has, and prints each use
#     that no longer compiles, macros included:
#       REMOVED <file>:<line> <name>
# Then one line per API (a function, type or macro), and the result:
#   API <name> <uses> uses in <files>
#   RESULT openssl3-api PASS|FAIL <n> deprecated and <n> removed uses of <n> APIs in <n> files, <n> sources checked
# A source that does not compile even with the deprecated APIs prints ERROR <file>:<line> <message>.
#
# Exit 0 when no source uses a deprecated API, 1 when one does or a source does not compile, 2 when
# the directory has no OpenSSL 3 headers, and 3 for bad arguments or when not in a configured trunk.

[[ $# -le 1 ]] || { echo "Usage: $0 [<OpenSSL 3 include dir>]"; exit 3; }
[[ -f objs/Makefile && -d src ]] || { echo "SETUP_FAIL: run it in a configured SRS trunk, with objs/Makefile"; exit 3; }

INC=$1
if [[ -z $INC ]]; then INC=$SRS_OPENSSL3_INCLUDE; fi
if [[ -z $INC ]] && BREW=$(brew --prefix openssl@3 2>/dev/null); then INC=$BREW/include; fi
if [[ -z $INC ]]; then INC=/usr/include; fi

CXX=${CXX:-c++}
# The version number is an expression in OpenSSL 3, so the preprocessor compares it.
VERSION=$(printf '%s\n' '#include <openssl/opensslv.h>' '#if OPENSSL_VERSION_NUMBER >= 0x30000000L' \
  'V3 OPENSSL_VERSION_TEXT' '#else' 'OLD OPENSSL_VERSION_TEXT' '#endif' |
  $CXX -E -P -x c++ -I"$INC" - 2>/dev/null | grep -E '^(V3|OLD) ' | tail -1)
TEXT=${VERSION#* }
TEXT=${TEXT//\"/}
if [[ -z $VERSION ]]; then
  echo "SKIP openssl3: no OpenSSL headers in $INC"
  exit 2
fi
if [[ $VERSION != V3* ]]; then
  echo "SKIP openssl3: $INC has $TEXT, not OpenSSL 3"
  exit 2
fi
echo "OPENSSL3 $INC ($TEXT)"

# The compiler's error limit option, so a source with many removed uses lists them all.
LIMIT=-fmax-errors=0
$CXX -ferror-limit=0 -fsyntax-only -x c++ /dev/null 2>/dev/null && LIMIT=-ferror-limit=0

START=$(date +%s)
WORK=objs/openssl3-api-check
mkdir -p $WORK || exit 3
TRUNK=$PWD

# One script per source and pass, the Makefile's command made syntax only, run 8 at a time.
OBJS=$(grep -o '^\./objs/src/[^:]*\.o:' objs/Makefile | sed 's/:$//')
JOBS=
NAMES=
for obj in $OBJS; do
  CMD=$(make -n -B -f objs/Makefile "$obj" 2>/dev/null |
    awk '/\\$/ { sub(/\\$/, ""); printf "%s", $0; next } { print }' | grep -F -- "-o $obj" | tail -1)
  name=$(echo "${obj#./objs/src/}" | tr '/' '_')
  NAMES="$NAMES $name"
  if [[ -z $CMD ]]; then
    echo "./${obj#./objs/}:0:0: error: no compile command for $obj in objs/Makefile" > $WORK/$name.p1
    : > $WORK/$name.p2
    continue
  fi
  CMD=$(echo "$CMD" | sed -e "s| -o ${obj//./\\.}||" -e "s|-I\./objs/openssl/include|-I$INC|" \
    -e 's/ -c / /' -e 's/ -fsanitize=[^ ]*//g')
  # A source that fails before the compiler names a file, such as a bad option, gets an error line.
  SRC=${CMD##* }
  echo "cd '$TRUNK' && { $CMD -fsyntax-only $LIMIT -fno-color-diagnostics > $WORK/$name.p1 2>&1 ||" \
    "grep -q ': error: ' $WORK/$name.p1 || echo '$SRC:0:0: error: the compile failed' >> $WORK/$name.p1; }" > $WORK/$name.p1.sh
  echo "cd '$TRUNK' && $CMD -fsyntax-only $LIMIT -fno-color-diagnostics -DOPENSSL_NO_DEPRECATED > $WORK/$name.p2 2>&1" > $WORK/$name.p2.sh
  JOBS="$JOBS $WORK/$name.p1.sh $WORK/$name.p2.sh"
done
[[ -n $JOBS ]] && echo $JOBS | tr ' ' '\n' | xargs -P 8 -n 1 bash

P1=
P2=
for name in $NAMES; do P1="$P1 $WORK/$name.p1"; P2="$P2 $WORK/$name.p2"; done

# <file>:<line>:<col>: warning|error: <message>, from sources under src only, with the ./ dropped.
# The function or type is the first quoted name; GCC quotes a declaration, so take the name before
# the (, and drop the * of a pointer type.
parse() {
  grep -h -E "^(\./)?src/[^:]+:[0-9]+:[0-9]+: $1: " $2 | grep -E "$3" |
    sed -E -e 's|^\./||' -e "s|^([^:]+):([0-9]+):[0-9]+: $1: [^']*'([^']*)'.*|\1:\2 \3|" |
    sed -E -e 's|^([^ ]+) (.*[ *&])?([A-Za-z_][A-Za-z0-9_]*)\(.*$|\1 \3|' \
      -e 's|^([^ ]+) ([A-Za-z_][A-Za-z0-9_]*)[ *&].*$|\1 \2|' | sort -u
}
DEPRECATED=$(parse warning "$P1" "is deprecated")
ERRORS=$(grep -h -E "^(\./)?src/[^:]+:[0-9]+:[0-9]+: error: " $P1 | sed -E -e 's|^\./||' \
  -e 's|^([^:]+):([0-9]+):[0-9]+: error: (.*)|\1:\2 \3|' | sort -u)
# Only the names that are gone; the other errors follow from them, such as a type mismatch.
REMOVED=$(parse error "$P2" "(unknown type name|use of undeclared identifier) '")
# A use that fails without OpenSSL 3 too is an ERROR, not a removed API.
if [[ -n $ERRORS ]]; then
  REMOVED=$(echo "$REMOVED" | grep -v -F -f <(echo "$ERRORS" | awk '{print $1 " "}'))
fi

[[ -n $DEPRECATED ]] && echo "$DEPRECATED" | sed 's/^/DEPRECATED /'
[[ -n $REMOVED ]] && echo "$REMOVED" | sed 's/^/REMOVED /'
[[ -n $ERRORS ]] && echo "$ERRORS" | sed 's/^/ERROR /'

USES=$(printf '%s\n%s\n' "$DEPRECATED" "$REMOVED" | grep . | sort -u)
APIS=0
FILES=0
if [[ -n $USES ]]; then
  for fn in $(echo "$USES" | awk '{print $2}' | sort -u); do
    LINES=$(echo "$USES" | awk -v f="$fn" '$2 == f {print $1}')
    IN=$(echo "$LINES" | sed 's/:[0-9]*$//' | sort -u | tr '\n' ' ')
    echo "API $fn $(echo "$LINES" | wc -l | tr -d ' ') uses in ${IN% }"
    APIS=$((APIS + 1))
  done
  FILES=$(echo "$USES" | sed 's/:[0-9]* .*//' | sort -u | wc -l | tr -d ' ')
fi

count() { [[ -n $1 ]] && echo "$1" | wc -l | tr -d ' ' || echo 0; }
ND=$(count "$DEPRECATED")
NR=$(count "$REMOVED")
NE=$(echo "$ERRORS" | grep . | sed 's/:.*//' | sort -u | wc -l | tr -d ' ')
SOURCES=$(echo $NAMES | wc -w | tr -d ' ')
DETAIL="$ND deprecated and $NR removed uses of $APIS APIs in $FILES files, $SOURCES sources checked"
[[ $NE != 0 ]] && DETAIL="$DETAIL, $NE do not compile"
DETAIL="$DETAIL ($(( $(date +%s) - START ))s)"
if [[ $ND == 0 && $NR == 0 && $NE == 0 ]]; then
  echo "RESULT openssl3-api PASS $DETAIL"
  exit 0
fi
echo "RESULT openssl3-api FAIL $DETAIL"
exit 1
