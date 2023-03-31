#!/usr/bin/env bash
set -e

NPROC=1

# OS detection
if [ "$(uname)" = "Linux" ]; then
	NPROC=$(nproc)
	CC=clang-12
elif [ "$(uname)" = "Darwin" ]; then
	NPROC=$(sysctl -n hw.ncpu)
	CC=clang
elif [ "$(uname)" = "FreeBSD" ]; then
	NPROC=$(sysctl -n hw.ncpu)
	CC=cc
else
	echo "Error: $(uname) not supported, sorry!"
	exit 1
fi

# Check if we have a compiler
if ! [ -x "$(command -v $CC)" ]; then
	echo "Error: $CC is not installed!" >&2
	exit 1
fi

echo "OS :" $(uname)
echo "CC :" $CC
echo "NP :" $NPROC

# Go to script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$SCRIPT_DIR"
cd ".."

pwd

# Find and then delete all files under current directory (.) that:
#  1. contains "cmake" (case-&insensitive) in its path (wholename)
#  2. name is not CMakeLists.txt
find . -iwholename '*cmake*' -not -name CMakeLists.txt -delete

# Build with ASAN / MSAN
cmake -Dsctp_build_fuzzer=1 -Dsctp_build_programs=0 -Dsctp_invariants=1 -Dsctp_sanitizer_address=1 -DCMAKE_LINKER="$CC" -DCMAKE_C_COMPILER="$CC" .
#cmake -Dsctp_build_fuzzer=1 -Dsctp_build_programs=0 -Dsctp_invariants=1 -Dsctp_sanitizer_memory=1 -DCMAKE_LINKER="$CC" -DCMAKE_C_COMPILER="$CC" .

make -j"$NPROC"
