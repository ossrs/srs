#!/bin/bash

# In .circleci/config.yml, generate *.gcno with
#       ./configure --gcov --without-research --without-librtmp && make
# and generate *.gcda by
#       ./objs/srs_utest

# Workdir is objs/cover.
workdir=`pwd`/objs/cover

# Tool git is required to map the right path.
git --version >/dev/null 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then echo "Tool git is required, ret=$ret"; exit $ret; fi

# Create trunk under workdir.
mkdir -p $workdir && cd $workdir
ret=$?; if [[ $ret -ne 0 ]]; then echo "Enter workdir failed, ret=$ret"; exit $ret; fi

# Collect all *.gcno and *.gcda to objs/cover.
cd $workdir && (rm -rf src && cp -R ../../src . && cp -R ../src/* src/)
ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect *.gcno and *.gcda failed, ret=$ret"; exit $ret; fi

# Generate *.gcov for coverage.
cd $workdir &&
for file in `find src -name "*.cpp"|grep -v utest`; do
    gcov $file -o `dirname $file`
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect $file failed, ret=$ret"; exit $ret; fi
done

# Cook the gcov files.
cd $workdir &&
find . -name "*.gcov"|grep -v srs|xargs rm -f
ret=$?; if [[ $ret -ne 0 ]]; then echo "Cook gcov files failed, ret=$ret"; exit $ret; fi

# Upload report with *.gcov
# Remark: The file codecov.yml is not neccessary. It literally depends on git.
# Note: The right path is like:
#       https://codecov.io/gh/ossrs/srs/src/3.0release/trunk/src/protocol/srs_rtmp_stack.cpp
#       https://codecov.io/gh/ossrs/srs/src/20fbb4466fdc8ba5d810b8570df6004063212838/trunk/src/protocol/srs_rtmp_stack.cpp
# Remark: It takes a few minutes to sync with github, so it might not available when CircleCI is done.
#       https://circleci.com/gh/ossrs/srs/tree/3.0release
cd $workdir &&
export CODECOV_TOKEN="493bba46-c468-4e73-8b45-8cdd8ff62d96" &&
bash <(curl -s https://codecov.io/bash) &&
echo "Done" && exit 0
