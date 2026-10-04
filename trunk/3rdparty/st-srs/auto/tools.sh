#!/bin/bash
#
# Build ST, then run every integration tool in tools/ under each event system.
#
# The build comes from the environment, empty by default:
#   EXTRA_CFLAGS  passed to the library build, such as -DMALLOC_STACK
#   LDFLAGS       passed to the tool link, such as -fsanitize=address
#
# Examples:
#   ./auto/tools.sh
#   EXTRA_CFLAGS=-DMALLOC_STACK ./auto/tools.sh
#   EXTRA_CFLAGS="-DMALLOC_STACK -DMD_ASAN -fsanitize=address -fno-omit-frame-pointer" \
#       LDFLAGS=-fsanitize=address ./auto/tools.sh
#
# Each tool reads ST_TOOL_EVENTSYS, select or alt. It prints "<name> <eventsys> OK"
# per run, or "FAILED <name> <eventsys>" with the output, and exits 1 at the first failure.

cd "$(dirname "$0")/.." || exit 1

ST_TARGET=linux-debug
if [[ $(uname -s) == Darwin ]]; then
    ST_TARGET=darwin-debug
fi

echo "Build ST with make -B $ST_TARGET EXTRA_CFLAGS=\"$EXTRA_CFLAGS\""
# -B rebuilds every object, so a change of flags never links a stale library.
if ! out=$(make -B $ST_TARGET EXTRA_CFLAGS="$EXTRA_CFLAGS" 2>&1); then
    echo "$out"
    echo "FAILED build ST"
    exit 1
fi

for dir in tools/*/; do
    name=$(basename "$dir")
    # -W relinks the tool, because the binaries are shared across platforms and builds.
    if ! out=$(make -C "$dir" -W "$name.c" LDFLAGS="$LDFLAGS" 2>&1); then
        echo "$out"
        echo "FAILED build $name"
        exit 1
    fi

    for eventsys in select alt; do
        if ! out=$(cd "$dir" && ST_TOOL_EVENTSYS=$eventsys "./$name" 2>&1); then
            echo "$out"
            echo "FAILED $name $eventsys"
            exit 1
        fi
        echo "$name $eventsys OK"
    done
done

echo "All tools OK"
