#!/bin/bash

# when options parsed, setup some variables, then build the depends.
OS_KERNEL_NAME=$(uname -s)
OS_KERNRL_RELEASE=$(uname -r|awk -F '-' '{print $1}')
OS_PREFIX="Platform"

if [[ $OSTYPE == cygwin ]]; then
    OS_KERNRL_RELEASE=$(uname -r|awk -F '(' '{print $1}')
fi

# Build platform cache.
SRS_PLATFORM="${SRS_BUILD_TAG}${OS_PREFIX}-${OS_KERNEL_NAME}-${OS_KERNRL_RELEASE}"
# Build platform cache with gcc version.
if [[ $OS_KERNEL_NAME == Darwin ]]; then
  GCC_VERSION="Clang$(gcc --version 2>/dev/null|grep clang|awk '{print $4}')"
  SRS_PLATFORM="${SRS_PLATFORM}-${GCC_VERSION}"
else
  GCC_VERSION="GCC$(gcc --version 2>/dev/null|grep gcc|awk '{print $3}')"
  echo $GCC_VERSION| grep '-' >/dev/null && GCC_VERSION=$(echo $GCC_VERSION| awk -F '-' '{print$1}')
  SRS_PLATFORM="${SRS_PLATFORM}-${GCC_VERSION}"
fi
# Use isolate cache for different SRS version.
SRS_PLATFORM="${SRS_PLATFORM}-SRS5-$(uname -m)"

if [[ $SRS_CROSS_BUILD == YES ]]; then
  SRS_PLATFORM="${SRS_PLATFORM}-CROSSBUILD-$(echo $SRS_TOOL_CC|awk -F - '{print $1}')"
fi

#TODO find the link lib in objs/Platform_some_platfomr/3rdpatry/some-lib
# 3rdparty lib path
# eg: objs/st, objs/ffmpeg etc.
SRS_3RD_ST_PATH=${SRS_OBJS}/st
SRS_3RD_FFMPEG_PATH=${SRS_OBJS}/ffmpeg
SRS_3RD_OPUS_PATH=${SRS_OBJS}/opus
SRS_3RD_SRTP2_PATH=${SRS_OBJS}/srtp2
SRS_3RD_OPENSSL_PATH=${SRS_OBJS}/openssl
SRS_3RD_SRT_PATH=${SRS_OBJS}/srt

# 3rdparty lib store path
# eg: objs/Platform-CYGWIN_NT-10.0-3.2.0-GCC11.2.0-SRS5-x86_64/3rdpatry/st
SRS_3RD_ST_STORE_PATH=${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/st
SRS_3RD_FFMPEG_STORE_PATH=${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/ffmpeg
SRS_3RD_OPUS_STORE_PATH=${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/opus
SRS_3RD_SRTP2_STORE_PATH=${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/srtp2
SRS_3RD_OPENSSL_STORE_PATH=${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/openssl
SRS_3RD_SRT_STORE_PATH=${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/srt

echo "SRS_WORKDIR: ${SRS_WORKDIR}, SRS_OBJS_DIR: ${SRS_OBJS_DIR}, SRS_OBJS: ${SRS_OBJS}, SRS_PLATFORM: ${SRS_PLATFORM}"

# For src object files on each platform.
(
    mkdir -p ${SRS_OBJS_DIR} && cd ${SRS_OBJS_DIR} &&
    rm -rf src utest srs srs_utest research include lib srs_hls_ingester srs_mp4_parser &&
    # on windows, clean *.exe
    rm -rf srs.exe srs_utest.exe srs_hls_ingester.exe srs_mp4_parser.exe &&
    mkdir -p ${SRS_PLATFORM}/src  &&
    mkdir -p ${SRS_PLATFORM}/research &&
    mkdir -p ${SRS_PLATFORM}/include &&
    mkdir -p ${SRS_PLATFORM}/lib &&
    mkdir -p ${SRS_PLATFORM}/utest &&
    
    rm -rf st ffmpeg opus srtp2 openssl srt &&
    # objs/some-lib
    mkdir -p ${SRS_3RD_ST_PATH} &&
    mkdir -p ${SRS_3RD_FFMPEG_PATH} &&
    mkdir -p ${SRS_3RD_OPUS_PATH} &&
    mkdir -p ${SRS_3RD_SRTP2_PATH} &&
    mkdir -p ${SRS_3RD_OPENSSL_PATH} &&
    mkdir -p ${SRS_3RD_SRT_STORE_PATH} &&
    
    # objs/Platform_some_platform/3rdpatry/some-lib
    mkdir -p ${SRS_3RD_ST_STORE_PATH} &&
    mkdir -p ${SRS_3RD_FFMPEG_STORE_PATH} &&
    mkdir -p ${SRS_3RD_OPUS_STORE_PATH} &&
    mkdir -p ${SRS_3RD_SRTP2_STORE_PATH} &&
    mkdir -p ${SRS_3RD_OPENSSL_STORE_PATH} &&
    mkdir -p ${SRS_3RD_SRT_STORE_PATH}
)
if [[ $SRS_CLEAN == NO ]]; then
  echo "Fast cleanup, if need to do full cleanup, please use: make clean"
fi

# Python or python2, for CentOS8.
python2 --version >/dev/null 2>&1 && alias python=python2 &&
echo "Alias python2 as python"

