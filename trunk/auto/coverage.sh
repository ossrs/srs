#!/bin/bash

# In .circleci/config.yml, generate *.gcno with
#       ./configure --gcov --without-research --without-librtmp && make
# and generate *.gcda by
#       ./objs/srs_utest

# Workdir is objs/cover.
workdir=`pwd`/objs/cover

# Create trunk under workdir.
mkdir -p $workdir && cd $workdir
ret=$?; if [[ $ret -ne 0 ]]; then echo "Enter workdir failed, ret=$ret"; exit $ret; fi

# Collect all *.gcno and *.gcda to objs/cover.
(rm -rf src && cp -R ../../src . && cp -R ../src .)
ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect *.gcno and *.gcda failed, ret=$ret"; exit $ret; fi

# Generate *.gcov for coverage.
for file in `find src -name "*.cpp"|grep -v utest`; do
    gcov $file -o `dirname $file`
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect $file failed, ret=$ret"; exit $ret; fi
done

# Cook the gcov files.
# The right path is https://codecov.io/gh/ossrs/srs/src/1b2aff84bc50f0681f37b959af6ecaed9490a95d/trunk/src/kernel/srs_kernel_codec.cpp
find . -name "*.gcov"|grep -v srs|xargs rm -f
ret=$?; if [[ $ret -ne 0 ]]; then echo "Cook gcov files failed, ret=$ret"; exit $ret; fi

# Upload report with *.gcov
cd $workdir &&
export CODECOV_TOKEN="493bba46-c468-4e73-8b45-8cdd8ff62d96" &&
bash <(curl -s https://codecov.io/bash) &&
echo "ok" && exit 0
