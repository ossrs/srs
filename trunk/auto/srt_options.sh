#!/bin/bash

# Set LIBSRT_OPTIONS, the options SRS builds libsrt-1-fit with. Sourced by auto/depends.sh, which builds libsrt
# with them and keeps them in srs-options.txt beside the built libsrt, and by scripts/check_build_cache.sh, which
# compares them with the cached libsrt's. Reads SRS_SHARED_SRT, SRS_CROSS_BUILD and SRS_TOOL_CC.

# Always disable c++11 for libsrt, because only the srt-app requres it. Always enable bonding (socket groups);
# SRS accepts a group only when srt_server.bonding is on.
LIBSRT_OPTIONS="--enable-apps=0  --enable-static=1 --enable-c++11=0 --enable-bonding=1"
CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d'.' -f1)
if [[ $CMAKE_MAJOR -ge 4 ]]; then
    LIBSRT_OPTIONS="$LIBSRT_OPTIONS --CMAKE_POLICY_VERSION_MINIMUM=3.5"
fi
if [[ $SRS_SHARED_SRT == YES ]]; then
    LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=1"
else
    LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=0"
fi

# For cross-build.
if [[ $SRS_CROSS_BUILD == YES ]]; then
    TOOL_GCC_REALPATH=$(realpath $(which $SRS_TOOL_CC))
    SRT_COMPILER_PREFIX=$(echo $TOOL_GCC_REALPATH |sed 's/-gcc.*$/-/')
    LIBSRT_OPTIONS="$LIBSRT_OPTIONS --with-compiler-prefix=$SRT_COMPILER_PREFIX"
fi
