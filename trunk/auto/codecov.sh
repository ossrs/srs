#!/bin/bash

# In .circleci/config.yml, generate *.gcno with
#       ./configure --gcov --without-research --without-librtmp && make utest
# and generate *.gcda by
#       ./objs/srs_utest

# Workdir is objs/cover.
workdir=`pwd`/objs/cover

# Create trunk under workdir.
mkdir -p $workdir && cd $workdir
ret=$?; if [[ $ret -ne 0 ]]; then echo "Enter workdir failed, ret=$ret"; exit $ret; fi

CODECOV_ARGS=""
if [[ $SRS_PROJECT != '' ]]; then
  # -R root dir  Used when not in git/hg project to identify project root directory
  # -p dir       Project root directory. Also used when preparing gcov
  CODECOV_ARGS="$CODECOV_ARGS -R $SRS_PROJECT -p $SRS_PROJECT"
fi
if [[ $SRS_BRANCH != '' ]]; then
  # -B branch    Specify the branch name
  CODECOV_ARGS="$CODECOV_ARGS -B $SRS_BRANCH"
fi
if [[ $SRS_SHA != '' ]]; then
  # -C sha       Specify the commit sha
  CODECOV_ARGS="$CODECOV_ARGS -C $SRS_SHA"
fi
if [[ $SRS_PR != '' ]]; then
  # -P pr        Specify the pull request number
  CODECOV_ARGS="$CODECOV_ARGS -P $SRS_PR"
fi

# Upload report with *.gcov
# Remark: The file codecov.yml is not neccessary. It literally depends on git.
# Note: The right path is like:
#       https://codecov.io/gh/ossrs/srs/src/3.0release/trunk/src/protocol/srs_rtmp_stack.cpp
#       https://codecov.io/gh/ossrs/srs/src/20fbb4466fdc8ba5d810b8570df6004063212838/trunk/src/protocol/srs_rtmp_stack.cpp
# Remark: It takes a few minutes to sync with github, so it might not available when CircleCI is done.
#       https://circleci.com/gh/ossrs/srs/tree/3.0release
cd $workdir &&
export CODECOV_TOKEN="493bba46-c468-4e73-8b45-8cdd8ff62d96" &&
bash <(curl -s https://codecov.io/bash) $CODECOV_ARGS &&
echo "Done" && exit 0
