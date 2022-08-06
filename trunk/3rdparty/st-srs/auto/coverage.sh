#!/bin/bash

if [[ ! -f utest/gtest-fit/googletest/include/gtest/gtest.h ]]; then
  echo "No utest/gtest, please download from https://github.com/google/googletest/releases/tag/release-1.11.0"
  exit -1
else
  echo "Check utest/gtest ok"
fi

if [[ $(gcovr --version >/dev/null && echo yes) != yes ]]; then
  echo "Please install gcovr"
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
(cd obj && rm -f gtest-all.gcda gtest-all.gcno) &&
(cd obj && rm -f *.c *.cpp gtest-fit && ln -sf ../*.c . && ln -sf ../utest/*.cpp && ln -sf ../utest/gtest-fit .) &&
(cd obj && gcovr --gcov-exclude gtest --html --html-details -o ../coverage/st.html) &&
(cd obj && rm -f *.c *.cpp gtest-fit) &&
echo "Coverage report at coverage/st.html" &&
open coverage/st.html
