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
    SRS_TOOL_CC_NAME=$(basename $SRS_TOOL_CC)
    SRS_PLATFORM="${SRS_PLATFORM}-crossbuild-$(echo $SRS_TOOL_CC_NAME|awk -F - '{print $1"-"$2}')"
fi

echo "SRS_WORKDIR: ${SRS_WORKDIR}, SRS_OBJS: ${SRS_OBJS}, SRS_PLATFORM: ${SRS_PLATFORM}"
echo "All outputs to: ${SRS_OBJS}/${SRS_PLATFORM}"

# For src object files on each platform.
(
    mkdir -p ${SRS_OBJS} &&
    (cd ${SRS_OBJS} && rm -rf src utest srs srs_utest research include lib srs_hls_ingester srs_mp4_parser) &&
    mkdir -p ${SRS_OBJS}/src ${SRS_OBJS}/research ${SRS_OBJS}/utest &&

    mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry &&
    (cd ${SRS_OBJS} && rm -rf st ffmpeg opus srtp2 openssl srt) &&
    mkdir -p ${SRS_OBJS}/st ${SRS_OBJS}/ffmpeg ${SRS_OBJS}/opus ${SRS_OBJS}/srtp2 ${SRS_OBJS}/openssl \
        ${SRS_OBJS}/srt &&
    mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/st ${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/ffmpeg \
        ${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/opus ${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/srtp2 \
        ${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/openssl ${SRS_OBJS}/${SRS_PLATFORM}/3rdpatry/srt
)
ret=$?; if [[ $ret -ne 0 ]]; then echo "Create build directory failed, ret=$ret"; exit $ret; fi

if [[ $SRS_CLEAN == NO ]]; then
  echo "Fast cleanup, if need to do full cleanup, please use: make clean"
fi

# TODO: FIXME: Remove python, use Go for api server demo.
# Python or python2, for CentOS8.
python2 --version >/dev/null 2>&1 && alias python=python2 &&
echo "Alias python2 as python"

