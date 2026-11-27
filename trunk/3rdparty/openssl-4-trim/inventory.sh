#!/bin/bash
# Build a copy of the openssl-4-fit tree with the --ssl-4=on options and list every file of the tree that
# the build read. A read is found by access time: every file's atime is set to 2000-01-01 first, so any
# read moves it past a marker. This finds Configure's .conf files, templates, build.info files and Perl
# modules, the sources and headers the compiler opens, and the headers install_sw copies.
#
# Usage: OPTIONS="<configure options>" inventory.sh <tree> <work-dir> <name> <make-CC> <configure args...>
#   e.g. inventory.sh /src /work linux-aarch64 gcc ./config
#        inventory.sh /src /work linux-armv4 arm-linux-gnueabihf-gcc ./Configure linux-armv4 \
#            --cross-compile-prefix=arm-linux-gnueabihf-
# Set CONFIGURE_ONLY=1 for a target with no toolchain: it configures and generates the Makefile only.
# Output in <work-dir>/<name>: read.txt (sorted paths), all.txt, configdata.pm, libs.txt and the logs.
set -u
SRC=$1; WORK=$2; NAME=$3; MAKE_CC=$4; shift 4
: "${OPTIONS:?set OPTIONS, see options.sh}"
OUT=$WORK/$NAME
rm -rf "$OUT" && mkdir -p "$OUT" || exit 1
cp -R "$SRC" "$OUT/tree" || exit 1
(cd "$OUT/tree" && find . -type f | sed 's|^\./||' | LC_ALL=C sort) > "$OUT/all.txt"
find "$OUT/tree" -type f -exec touch -a -t 200001010000 {} + || exit 1
# A marker newer than the reset atimes and older than any read.
sleep 1; touch "$OUT/start.marker"; sleep 1

[[ $(uname -s) == Darwin ]] && export KERNEL_BITS=64
JOBS=${JOBS:-8}
T0=$(date +%s)
(cd "$OUT/tree" && chmod +x ./config ./Configure && "$@" --prefix="$OUT/prefix" $OPTIONS) > "$OUT/configure.log" 2>&1
r=$?; if [[ $r -ne 0 ]]; then echo "$NAME: configure failed ($r)"; tail -20 "$OUT/configure.log"; exit 1; fi
T1=$(date +%s)
if [[ ${CONFIGURE_ONLY:-0} == 1 ]]; then
    [[ -f $OUT/tree/Makefile ]] || { echo "$NAME: no Makefile"; exit 1; }
    echo "$NAME: configure only"
else
    make -C "$OUT/tree" CC="$MAKE_CC" -j"$JOBS" > "$OUT/make.log" 2>&1
    r=$?; if [[ $r -ne 0 ]]; then echo "$NAME: make failed ($r)"; tail -30 "$OUT/make.log"; exit 1; fi
    make -C "$OUT/tree" install_sw > "$OUT/install.log" 2>&1
    r=$?; if [[ $r -ne 0 ]]; then echo "$NAME: install_sw failed ($r)"; tail -20 "$OUT/install.log"; exit 1; fi
fi
T2=$(date +%s)
cp "$OUT/tree/configdata.pm" "$OUT/"
# Files of the tree read since the marker; generated files are not in all.txt.
(cd "$OUT/tree" && find . -type f -anewer "$OUT/start.marker" | sed 's|^\./||' | LC_ALL=C sort) > "$OUT/anewer.txt"
LC_ALL=C comm -12 "$OUT/all.txt" "$OUT/anewer.txt" > "$OUT/read.txt"
ls -l "$OUT/prefix/lib/libcrypto.a" "$OUT/prefix/lib/libssl.a" 2>/dev/null | awk '{print $5, $NF}' > "$OUT/libs.txt"
echo "$NAME: configure $((T1-T0))s, build $((T2-T1))s, read $(wc -l < "$OUT/read.txt") of $(wc -l < "$OUT/all.txt") files"
