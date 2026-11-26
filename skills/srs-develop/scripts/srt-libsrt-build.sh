#!/bin/bash
#
# Build the SRT test client tools/srt-libsrt: Haivision libsrt v1.5.7, with bonding and encryption, from source
# into tools/srt-libsrt/objs, then the client on it. The client is a test peer of SRS, so it uses its own libsrt,
# a newer release than SRS bundles in trunk/3rdparty, never SRS's build.
#
# Usage:
#   bash skills/srs-develop/scripts/srt-libsrt-build.sh
#
# Output:
#   tools/srt-libsrt/objs/libsrt         libsrt installed: include/srt, lib/libsrt.a, lib/pkgconfig/srt.pc
#   tools/srt-libsrt/objs/srt-libsrt     the client, see tools/srt-libsrt/README.md
#
# libsrt is built once: a later run reuses it while objs/libsrt/srs-options.txt holds the same version, platform
# and CMake options, and only rebuilds the client when its sources changed. Remove tools/srt-libsrt/objs to start
# over. CI builds it in a Docker volume mounted at tools/srt-libsrt/objs, as cmd/integ/README.md shows.
#
# Needs curl, cmake, a C++ compiler, pkg-config, Go and OpenSSL: on macOS Homebrew's openssl@3, on Linux the
# system's, such as libssl-dev.
#
# Exit code is 0 when the client is built, 1 otherwise.
set -e

SRT_VERSION=1.5.7
SRT_SHA256=017cd1e437ef2073a4dd10ddf7b55e86bc3d6ebac0393d13bd22f6a57055d32b
SRT_URL="https://github.com/Haivision/srt/archive/refs/tags/v${SRT_VERSION}.tar.gz"

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TOOL="$ROOT/tools/srt-libsrt"
OBJS="$TOOL/objs"
PREFIX="$OBJS/libsrt"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

for cmd in curl cmake pkg-config go make; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Error: $cmd not found in PATH." >&2
    exit 1
  fi
done

CMAKE_OPTIONS="-DCMAKE_BUILD_TYPE=Release -DENABLE_BONDING=ON -DENABLE_ENCRYPTION=ON -DUSE_ENCLIB=openssl-evp"
CMAKE_OPTIONS="$CMAKE_OPTIONS -DENABLE_APPS=OFF -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DENABLE_CXX_DEPS=ON"
# The platform too, so a libsrt built on another OS or CPU, such as the Mac's in a Linux container, is rebuilt.
OPTIONS="srt ${SRT_VERSION} $(uname -sm) ${CMAKE_OPTIONS}"

OPENSSL_OPTIONS=""
if [[ "$(uname -s)" == "Darwin" ]]; then
  if ! command -v brew >/dev/null 2>&1 || ! brew --prefix openssl@3 >/dev/null 2>&1; then
    echo "Error: OpenSSL not found; install it with: brew install openssl@3" >&2
    exit 1
  fi
  OPENSSL_OPTIONS="-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)"
fi

if [[ -f "$PREFIX/lib/libsrt.a" && "$(cat "$PREFIX/srs-options.txt" 2>/dev/null)" == "$OPTIONS" ]]; then
  echo "libsrt ${SRT_VERSION} is built in $PREFIX"
else
  echo "Build libsrt ${SRT_VERSION} into $PREFIX"
  mkdir -p "$OBJS"
  TARBALL="$OBJS/srt-${SRT_VERSION}.tar.gz"
  if [[ ! -f "$TARBALL" ]]; then
    curl -fsSL -o "$TARBALL.part" "$SRT_URL"
    mv "$TARBALL.part" "$TARBALL"
  fi
  GOT_SHA256="$( (sha256sum "$TARBALL" 2>/dev/null || shasum -a 256 "$TARBALL") | awk '{print $1}')"
  if [[ "$GOT_SHA256" != "$SRT_SHA256" ]]; then
    echo "Error: $TARBALL has SHA256 $GOT_SHA256, want $SRT_SHA256; remove it to download again." >&2
    exit 1
  fi

  tar -xzf "$TARBALL" -C "$OBJS"
  # shellcheck disable=SC2086
  cmake -S "$OBJS/srt-${SRT_VERSION}" -B "$OBJS/build-${SRT_VERSION}" $CMAKE_OPTIONS $OPENSSL_OPTIONS \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib >"$OBJS/cmake.log" 2>&1 || {
    tail -n 30 "$OBJS/cmake.log" >&2
    echo "Error: cmake failed, see $OBJS/cmake.log" >&2
    exit 1
  }
  cmake --build "$OBJS/build-${SRT_VERSION}" --parallel "$JOBS" --target install >"$OBJS/build.log" 2>&1 || {
    tail -n 30 "$OBJS/build.log" >&2
    echo "Error: the libsrt build failed, see $OBJS/build.log" >&2
    exit 1
  }
  echo "$OPTIONS" >"$PREFIX/srs-options.txt"
fi

make -C "$TOOL" build
echo "Built $OBJS/srt-libsrt"
