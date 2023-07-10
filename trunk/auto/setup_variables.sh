#!/bin/bash

# when options parsed, setup some variables, then build the depends.
OS_KERNEL_NAME=$(uname -s)
OS_KERNRL_RELEASE=$(uname -r|awk -F '-' '{print $1}')
OS_PREFIX="Platform"

# Detect gcc, which is required.
gcc --version >/dev/null 2>/dev/null || (ret=$?; echo "Please install gcc"; exit $ret)

# Discover SRS version from header file.
mkdir -p ${SRS_OBJS} &&
echo '#include <stdio.h>' > ${SRS_OBJS}/test_version.c &&
echo '#include <srs_core_version.hpp>' >> ${SRS_OBJS}/test_version.c &&
echo 'int main(int argc, char** argv) {' >> ${SRS_OBJS}/test_version.c &&
echo '      printf("%d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);' >> ${SRS_OBJS}/test_version.c &&
echo '      return 0;' >> ${SRS_OBJS}/test_version.c &&
echo '}' >> ${SRS_OBJS}/test_version.c &&
gcc -g -O0 ${SRS_OBJS}/test_version.c -I${SRS_WORKDIR}/src/core -o ${SRS_OBJS}/test_version 1>/dev/null 2>&1 &&
SRS_VERSION=$(./${SRS_OBJS}/test_version 2>/dev/null) &&
SRS_MAJOR=$(echo $SRS_VERSION |awk -F . '{print $1}');
ret=$?; rm -rf ${SRS_OBJS}/test_version*; if [[ $ret -ne 0 ]]; then echo "Detect SRS version failed"; exit $ret; fi
echo "Discover SRS version=${SRS_VERSION}, major=${SRS_MAJOR}"

if [[ $OSTYPE == cygwin ]]; then
    OS_KERNRL_RELEASE=$(uname -r|awk -F '(' '{print $1}')
fi

# Build platform cache.
SRS_PLATFORM="${SRS_BUILD_TAG}${OS_PREFIX}-SRS${SRS_MAJOR}-${OS_KERNEL_NAME}-${OS_KERNRL_RELEASE}"
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
SRS_PLATFORM="${SRS_PLATFORM}-$(uname -m)"

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

    mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty &&
    (cd ${SRS_OBJS} && rm -rf st ffmpeg opus srtp2 openssl srt) &&
    mkdir -p ${SRS_OBJS}/st ${SRS_OBJS}/ffmpeg ${SRS_OBJS}/opus ${SRS_OBJS}/srtp2 ${SRS_OBJS}/openssl \
        ${SRS_OBJS}/srt &&
    mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/ffmpeg \
        ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/opus ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srtp2 \
        ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt
)
ret=$?; if [[ $ret -ne 0 ]]; then echo "Create build directory failed, ret=$ret"; exit $ret; fi

if [[ $SRS_CLEAN == NO ]]; then
  echo "Fast cleanup, if need to do full cleanup, please use: make clean"
fi

# TODO: FIXME: Remove python, use Go for api server demo.
# Python or python2, for CentOS8.
python2 --version >/dev/null 2>&1 && alias python=python2 &&
echo "Alias python2 as python"

