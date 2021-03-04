#!/bin/bash

MODULES=""
if [[ $# -gt 0 ]]; then
  for module in $@; do
    module=$(basename $module)
    if [[ $module == "src" ]]; then
      MODULES="objs/src"
      break
    fi
    MODULES="$MODULES objs/src/$module"
  done
fi

if [[ $MODULES == "" ]]; then
  MODULES="objs/src"
fi

IS_LINUX=yes
uname -s|grep Darwin >/dev/null && IS_DARWIN=yes && IS_LINUX=no
echo "IS_LINUX: $IS_LINUX, IS_DARWIN: $IS_DARWIN"

echo "Clean gcda files"
find objs -name *.gcda |xargs rm -f

echo "Build and run utest"
make -j10 && ./objs/srs_utest

echo "Generating coverage at $MODULES"
mkdir -p ./objs/coverage &&
gcovr -r src --html --html-details -o ./objs/coverage/srs.html $MODULES &&
echo "Coverage report at ./objs/coverage/srs.html" &&
open ./objs/coverage/srs.html
