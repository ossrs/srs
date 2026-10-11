#!/bin/bash
#
# Build and run the C++ unit tests of this checkout, from trunk/ so the cwd-relative test data is found.
#
# Usage:
#   bash skills/srs-develop/scripts/srs-utest-run.sh [--configure] [gtest-filter]
#
#   --configure     Re-run ./configure with the Full-tier flags, then remove the utest objects, which do not
#                   depend on the configure header. Done on its own in a new checkout or after trunk/configure changes.
#   gtest-filter    Run only these tests, such as 'ForwarderTest.*'. Default: all tests.
#
# Exit code is the exit code of srs_utest, or 1 if the build fails.

FLAGS="--srt=on --gb28181=on --rtsp=on --apm=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on"

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cd "$ROOT/trunk" || exit 1

CONFIGURE=no
FILTER=
for arg in "$@"; do
    case "$arg" in
        --configure) CONFIGURE=yes ;;
        *) FILTER="$arg" ;;
    esac
done

if [[ ! -f objs/Makefile || configure -nt objs/Makefile ]]; then
    CONFIGURE=yes
fi

if [[ $CONFIGURE == yes ]]; then
    echo "configure: $FLAGS"
    if ! ./configure $FLAGS >objs-configure.log 2>&1; then
        tail -n 30 objs-configure.log
        echo "configure failed, log trunk/objs-configure.log"
        exit 1
    fi
    rm -f objs-configure.log
    find objs -path '*/utest/*.o' -delete
fi

HEADER=$(grep SRS_USER_CONFIGURE objs/srs_auto_headers.hpp | sed 's/.*"\(.*\)"/\1/')
if [[ "$HEADER" != "$FLAGS" ]]; then
    echo "objs is configured with '$HEADER', not the Full-tier flags; run with --configure"
    exit 1
fi

echo "build: make utest"
if ! make -j10 utest >objs/utest-build.log 2>&1; then
    grep -E 'error:|Error ' objs/utest-build.log | head -n 30
    echo "build failed, log trunk/objs/utest-build.log"
    exit 1
fi

if [[ -n "$FILTER" ]]; then
    ./objs/srs_utest --gtest_filter="$FILTER"
else
    ./objs/srs_utest
fi
