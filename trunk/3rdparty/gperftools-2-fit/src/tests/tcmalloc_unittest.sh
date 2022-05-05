#!/bin/sh

# Copyright (c) 2013, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# ---
# Author: Adhemerval Zanella
#
# Runs the tcmalloc_unittest with various environment variables.
# This is necessary because tuning some environment variables
# (TCMALLOC_TRANSFER_NUM_OBJ for instance) should not change program
# behavior, just performance.

BINDIR="${BINDIR:-.}"
TCMALLOC_UNITTEST="${1:-$BINDIR/tcmalloc_unittest}"

TMPDIR=/tmp/tcmalloc_unittest
rm -rf $TMPDIR || exit 2
mkdir $TMPDIR || exit 3

run_unittest() {
    if $TCMALLOC_UNITTEST > $TMPDIR/output 2>&1; then
      echo "OK"
    else
      echo "FAILED"
      echo "Output from the failed run:"
      echo "----"
      cat $TMPDIR/output
      echo "----"
      exit 4
    fi
}

# $1: value of tcmalloc_unittest env. var.
run_check_transfer_num_obj() {
    [ -n "$1" ] && export TCMALLOC_TRANSFER_NUM_OBJ="$1"

    echo -n "Testing $TCMALLOC_UNITTEST with TCMALLOC_TRANSFER_NUM_OBJ=$1 ... "
    run_unittest
}

run_check_transfer_num_obj ""
run_check_transfer_num_obj "40"
run_check_transfer_num_obj "4096"

echo -n "Testing $TCMALLOC_UNITTEST with TCMALLOC_AGGRESSIVE_DECOMMIT=t ... "

TCMALLOC_AGGRESSIVE_DECOMMIT=t run_unittest

echo -n "Testing $TCMALLOC_UNITTEST with TCMALLOC_HEAP_LIMIT_MB=512 ... "

TCMALLOC_HEAP_LIMIT_MB=512 run_unittest

echo -n "Testing $TCMALLOC_UNITTEST with TCMALLOC_ENABLE_SIZED_DELETE=t ..."

TCMALLOC_ENABLE_SIZED_DELETE=t run_unittest

echo "PASS"
