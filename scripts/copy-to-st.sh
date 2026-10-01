#!/bin/bash

#
# Copy from the SRS mirror trunk/3rdparty/st-srs to State Threads.
#

SRS_WORK_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SRS_ST="$SRS_WORK_DIR/trunk/3rdparty/st-srs"
ST_DIR="$SRS_WORK_DIR/state-threads"

# Files that only State Threads owns, never overwritten from the mirror.
st_only() {
  case "$1" in
    .gitignore|.github/*|.run/*|ide/*|utest/gtest-fit/*) return 0 ;;
    *) return 1 ;;
  esac
}

if [[ ! -f "$SRS_ST/public.h" ]]; then
  echo "no st-srs in $SRS_ST"
  exit -1
fi

if [[ ! -f "$ST_DIR/public.h" ]]; then
  echo "no State Threads project in $ST_DIR"
  exit -1
fi

SOURCES=$(mktemp) && TARGETS=$(mktemp)
trap 'rm -f "$SOURCES" "$TARGETS"' EXIT

(cd "$SRS_ST" && git ls-files --cached --others --exclude-standard) | sort -u | while read -r name; do
  st_only "$name" || [[ ! -f "$SRS_ST/$name" ]] || echo "$name"
done > "$SOURCES"
git -C "$ST_DIR" ls-files --cached --others --exclude-standard | sort -u | while read -r name; do
  st_only "$name" || echo "$name"
done > "$TARGETS"

while read -r name; do
  if ! (mkdir -p "$(dirname "$ST_DIR/$name")" && cp "$SRS_ST/$name" "$ST_DIR/$name"); then
    echo "copy $name failed"
    exit -1
  fi
done < "$SOURCES" || exit -1
echo "Copy files success"

comm -13 "$SOURCES" "$TARGETS" | while read -r name; do
  if ! rm -f "$ST_DIR/$name"; then
    echo "remove $name failed"
    exit -1
  fi
  echo "Removed $name"
done || exit -1
echo "Remove stale files success"

echo "Done"
