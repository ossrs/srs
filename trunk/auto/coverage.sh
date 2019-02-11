#!/bin/bash

# In .circleci/config.yml, generate *.gcno with
#       ./configure --gcov --without-research --without-librtmp
# and generate *.gcda by
#       ./objs/srs_utest

# Collect all *.gcno and *.gcda to objs/cover.
(mkdir -p objs/cover && cd objs/cover &&
cp -R ../../src . &&
for file in `find ../src -name "*.gcno"`; do cp $file .; done &&
for file in `find ../src -name "*.gcda"`; do cp $file .; done)
ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect *.gcno and *.gcda failed, ret=$ret"; exit $ret; fi

# Generate *.gcov to objs/cover
for file in `find src -name "*.cpp"`; do
    (mkdir -p objs/cover && cd objs/cover && gcov ../../$file -o .)
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Collect $file failed, ret=$ret"; exit $ret; fi
done

# Upload report with *.gcov
export CODECOV_TOKEN="493bba46-c468-4e73-8b45-8cdd8ff62d96" &&
mkdir -p objs/cover && cd objs/cover &&
bash <(curl -s https://codecov.io/bash)
exit 0
