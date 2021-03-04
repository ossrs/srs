#!/bin/bash

IS_LINUX=yes
uname -s|grep Darwin >/dev/null && IS_DARWIN=yes && IS_LINUX=no
echo "IS_LINUX: $IS_LINUX, IS_DARWIN: $IS_DARWIN"

echo "Clean gcda files"
find objs -name *.gcda |xargs rm -f

echo "Build and run utest"
make -j10 && ./objs/srs_utest

echo "Generating coverage"
mkdir -p ./objs/coverage &&
gcovr -r src --html --html-details -o ./objs/coverage/srs.html objs/src &&
echo "Coverage report at ./objs/coverage/srs.html" &&
open ./objs/coverage/srs.html
