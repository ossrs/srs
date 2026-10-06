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
#   ./auto/tools.sh tcp udp
#   EXTRA_CFLAGS=-DMALLOC_STACK ./auto/tools.sh
#   EXTRA_CFLAGS="-DMALLOC_STACK -DMD_ASAN -fsanitize=address -fno-omit-frame-pointer" \
#       LDFLAGS=-fsanitize=address ./auto/tools.sh
#
# The arguments name the tools to run, every folder in tools/ by default.
# Each tool reads ST_TOOL_EVENTSYS, select or alt. It prints "<name> <eventsys> OK"
# per run, or "FAILED <name> <eventsys>" with the output, and exits 1 at the first failure.
# A tool that this platform does not support prints only "SKIP <name>: <why>" and exits 0,
# which shows as "SKIP <name>: <why> (<eventsys>)".
#
# On native Windows, run it from Git Bash with the MSVC environment on PATH, where
# uname -s is MINGW64_NT-* or MSYS_NT-*: it builds win64-debug and runs <name>.exe.

cd "$(dirname "$0")/.." || exit 1

ST_TARGET=linux-debug
EXE=
case $(uname -s) in
    Darwin) ST_TARGET=darwin-debug ;;
    MINGW*|MSYS*) ST_TARGET=win64-debug; EXE=.exe ;;
esac

tool_dirs=()
for name in "$@"; do
    if [[ ! -f tools/$name/Makefile ]]; then
        echo "FAILED no tool $name in tools/"
        exit 1
    fi
    tool_dirs+=("tools/$name/")
done
if [[ ${#tool_dirs[@]} == 0 ]]; then
    tool_dirs=(tools/*/)
fi

echo "Build ST with make -B $ST_TARGET EXTRA_CFLAGS=\"$EXTRA_CFLAGS\""
# -B rebuilds every object, so a change of flags never links a stale library.
if ! out=$(make -B $ST_TARGET EXTRA_CFLAGS="$EXTRA_CFLAGS" 2>&1); then
    echo "$out"
    echo "FAILED build ST"
    exit 1
fi

for dir in "${tool_dirs[@]}"; do
    name=$(basename "$dir")
    # -W relinks the tool, because the binaries are shared across platforms and builds.
    # A tool is C, <name>.c, or C++, <name>.cpp.
    src=$name.c
    if [[ -f $dir$name.cpp ]]; then
        src=$name.cpp
    fi
    if ! out=$(make -C "$dir" -W "$src" LDFLAGS="$LDFLAGS" 2>&1); then
        echo "$out"
        echo "FAILED build $name"
        exit 1
    fi

    for eventsys in select alt; do
        if ! out=$(cd "$dir" && ST_TOOL_EVENTSYS=$eventsys "./$name$EXE" 2>&1); then
            echo "$out"
            echo "FAILED $name $eventsys"
            exit 1
        fi
        if [[ $out == "SKIP $name:"* ]]; then
            echo "$out ($eventsys)"
            continue
        fi
        echo "$name $eventsys OK"
    done
done

echo "All tools OK"
