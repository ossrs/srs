#!/bin/bash

# Workdir is obj/coverage.
workdir=`pwd`/coverage

# Create trunk under workdir.
mkdir -p $workdir && cd $workdir
ret=$?; if [[ $ret -ne 0 ]]; then echo "Enter workdir failed, ret=$ret"; exit $ret; fi

CODECOV_ARGS=""
if [[ $ST_PROJECT != '' ]]; then
  # -R root dir  Used when not in git/hg project to identify project root directory
  # -p dir       Project root directory. Also used when preparing gcov
  CODECOV_ARGS="$CODECOV_ARGS -R $ST_PROJECT -p $ST_PROJECT"
fi
if [[ $ST_BRANCH != '' ]]; then
  # -B branch    Specify the branch name
  CODECOV_ARGS="$CODECOV_ARGS -B $ST_BRANCH"
fi
if [[ $ST_SHA != '' ]]; then
  # -C sha       Specify the commit sha
  CODECOV_ARGS="$CODECOV_ARGS -C $ST_SHA"
fi
if [[ $ST_PR != '' ]]; then
  # -P pr        Specify the pull request number
  CODECOV_ARGS="$CODECOV_ARGS -P $ST_PR"
fi

# Upload report with *.gcov
# Remark: The file codecov.yml is not neccessary. It literally depends on git.
# Note: The right path is like:
#       https://app.codecov.io/gh/ossrs/state-threads/blob/srs/sched.c
#       https://app.codecov.io/gh/ossrs/state-threads/blob/593cf748f055ca383867003e409a423efd8f8f86/sched.c
cd $workdir &&
export CODECOV_TOKEN="$CODECOV_TOKEN" &&
bash <(curl -s https://codecov.io/bash) $CODECOV_ARGS -f '!*gtest*' -f '!*c++*' &&
echo "Done" && exit 0

