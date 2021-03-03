#!/bin/bash

if [[ ! -f utest/gtest/include/gtest/gtest.h ]]; then
  (
    cd utest && rm -rf gtest &&
    curl https://github.com/google/googletest/archive/release-1.6.0.tar.gz -L -o googletest-release-1.6.0.tar.gz &&
    tar xf googletest-release-1.6.0.tar.gz &&
    ln -sf googletest-release-1.6.0 gtest &&
    echo "Setup gtest ok"
  )
fi
if [[ ! -f utest/gtest/include/gtest/gtest.h ]]; then
  echo "No utest/gtest, please download from https://github.com/google/googletest/releases/tag/release-1.6.0"
  exit -1
else
  echo "Check utest/gtest ok"
fi

if [[ $(gcovr --version >/dev/null && echo yes) != yes ]]; then
  echo "Please install gcovr: https://github.com/ossrs/state-threads/tree/srs#utest-and-coverage"
  exit -1
fi

IS_LINUX=yes
uname -s|grep Darwin >/dev/null && IS_DARWIN=yes && IS_LINUX=no
echo "IS_LINUX: $IS_LINUX, IS_DARWIN: $IS_DARWIN"

echo "Build and run utest"
if [[ $IS_DARWIN == yes ]]; then
  make clean && make darwin-debug-gcov && ./obj/st_utest
else
  make clean && make linux-debug-gcov && ./obj/st_utest
fi

echo "Generating coverage"
mkdir -p coverage &&
gcovr -r . -e LINUX -e DARWIN -e examples --html --html-details -o coverage/st.html &&
echo "Coverage report at coverage/st.html" &&
open coverage/st.html
