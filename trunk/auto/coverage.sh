#!/bin/bash

# In .circleci/config.yml, generate *.gcno with
#       ./configure --gcov --without-research --without-librtmp && make
# and generate *.gcda by
#       ./objs/srs_utest

# Workdir is objs/cover.
mkdir -p objs/cover && cd objs/cover
ret=$?; if [[ $ret -ne 0 ]]; then echo "Enter workdir failed, ret=$ret"; exit $ret; fi

# Collect all *.gcno and *.gcda to objs/cover.
(rm -rf src && cp -R ../../src . && cp -R ../src .)
ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect *.gcno and *.gcda failed, ret=$ret"; exit $ret; fi

# Generate *.gcov for coverage.
for file in `find src -name "*.cpp"`; do
    gcov $file -o `dirname $file`
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect $file failed, ret=$ret"; exit $ret; fi
done

# Upload report with *.gcov
export CODECOV_TOKEN="493bba46-c468-4e73-8b45-8cdd8ff62d96" &&
bash <(curl -s https://codecov.io/bash)
exit 0
