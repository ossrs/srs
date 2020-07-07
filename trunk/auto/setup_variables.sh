#!/bin/bash

# when options parsed, setup some variables, then build the depends.
OS_KERNEL_NAME=$(uname -s)
OS_KERNRL_RELEASE=$(uname -r|awk -F '-' '{print $1}')
OS_PREFIX="Platform"

# Build platform cache.
SRS_PLATFORM="${OS_PREFIX}-${OS_KERNEL_NAME}-${OS_KERNRL_RELEASE}"
if [[ ${SRS_BUILD_TAG} != "" ]]; then
  SRS_PLATFORM="${SRS_PLATFORM}-${SRS_BUILD_TAG}"
fi
# Use isolate cache for different SRS version.
SRS_PLATFORM="${SRS_PLATFORM}-SRS4"

echo "SRS_WORKDIR: ${SRS_WORKDIR}, SRS_OBJS_DIR: ${SRS_OBJS_DIR}, SRS_OBJS: ${SRS_OBJS}, SRS_PLATFORM: ${SRS_PLATFORM}"

# For src object files on each platform.
(
    mkdir -p ${SRS_OBJS_DIR} && cd ${SRS_OBJS_DIR} &&
    rm -rf src utest srs srs_utest research include lib srs_hls_ingester srs_mp4_parser &&
    mkdir -p ${SRS_PLATFORM}/src && ln -sf ${SRS_PLATFORM}/src &&
    mkdir -p ${SRS_PLATFORM}/utest && ln -sf ${SRS_PLATFORM}/utest &&
    mkdir -p ${SRS_PLATFORM}/research && ln -sf ${SRS_PLATFORM}/research &&
    mkdir -p ${SRS_PLATFORM}/include && ln -sf ${SRS_PLATFORM}/include &&
    mkdir -p ${SRS_PLATFORM}/lib && ln -sf ${SRS_PLATFORM}/lib
)
if [[ $SRS_CLEAN == NO ]]; then
  echo "Fast cleanup, if need to do full cleanup, please use: make clean"
fi

# Python or python2, for CentOS8.
python2 --version >/dev/null 2>&1 && alias python=python2 &&
echo "Alias python2 as python"

