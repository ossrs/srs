#!/bin/sh
# 
# usage: rtpw_test <rtpw_commands>
# 
# tests the rtpw sender and receiver functions
#
# Copyright (c) 2001-2017, Cisco Systems, Inc.
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 
#   Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# 
#   Redistributions in binary form must reproduce the above
#   copyright notice, this list of conditions and the following
#   disclaimer in the documentation and/or other materials provided
#   with the distribution.
# 
#   Neither the name of the Cisco Systems, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived
#   from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
#

case $(uname -s) in
    *CYGWIN*|*MINGW*)
        EXE=".exe"
        ;;
    *Linux*)
        EXE=""
        export LD_LIBRARY_PATH=$CRYPTO_LIBDIR
        ;;
    *Darwin*)
        EXE=""
        export DYLD_LIBRARY_PATH=$CRYPTO_LIBDIR
        ;;
esac

RTPW=./rtpw$EXE
DEST_PORT=9999
DURATION=3

# First, we run "killall" to get rid of all existing rtpw processes.
# This step also enables this script to clean up after itself; if this
# script is interrupted after the rtpw processes are started but before
# they are killed, those processes will linger.  Re-running the script
# will get rid of them.

killall rtpw 2>/dev/null

if test -x $RTPW; then

GCMARGS128="-k 01234567890123456789012345678901234567890123456789012345 -g -e 128"
echo  $0 ": starting GCM mode 128-bit rtpw receiver process... "

exec $RTPW $* $GCMARGS128 -r 127.0.0.1 $DEST_PORT &

receiver_pid=$!

echo $0 ": receiver PID = $receiver_pid"

sleep 1 

# verify that the background job is running
ps -e | grep -q $receiver_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 254
fi

echo  $0 ": starting GCM 128-bit rtpw sender process..."

exec $RTPW $* $GCMARGS128 -s 127.0.0.1 $DEST_PORT  &

sender_pid=$!

echo $0 ": sender PID = $sender_pid"

# verify that the background job is running
ps -e | grep -q $sender_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 255
fi

sleep $DURATION

kill $receiver_pid
kill $sender_pid

wait $receiver_pid 2>/dev/null
wait $sender_pid 2>/dev/null

GCMARGS128="-k 01234567890123456789012345678901234567890123456789012345 -g -t 16 -e 128"
echo  $0 ": starting GCM mode 128-bit (16 byte tag) rtpw receiver process... "

exec $RTPW $* $GCMARGS128 -r 127.0.0.1 $DEST_PORT &

receiver_pid=$!

echo $0 ": receiver PID = $receiver_pid"

sleep 1 

# verify that the background job is running
ps -e | grep -q $receiver_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 254
fi

echo  $0 ": starting GCM 128-bit (16 byte tag) rtpw sender process..."

exec $RTPW $* $GCMARGS128 -s 127.0.0.1 $DEST_PORT  &

sender_pid=$!

echo $0 ": sender PID = $sender_pid"

# verify that the background job is running
ps -e | grep -q $sender_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 255
fi

sleep $DURATION

kill $receiver_pid
kill $sender_pid

wait $receiver_pid 2>/dev/null
wait $sender_pid 2>/dev/null


GCMARGS256="-k 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567 -g -e 256"
echo  $0 ": starting GCM mode 256-bit rtpw receiver process... "

exec $RTPW $* $GCMARGS256 -r 127.0.0.1 $DEST_PORT &

receiver_pid=$!

echo $0 ": receiver PID = $receiver_pid"

sleep 1 

# verify that the background job is running
ps -e | grep -q $receiver_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 254
fi

echo  $0 ": starting GCM 256-bit rtpw sender process..."

exec $RTPW $* $GCMARGS256 -s 127.0.0.1 $DEST_PORT  &

sender_pid=$!

echo $0 ": sender PID = $sender_pid"

# verify that the background job is running
ps -e | grep -q $sender_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 255
fi

sleep $DURATION

kill $receiver_pid
kill $sender_pid

wait $receiver_pid 2>/dev/null
wait $sender_pid 2>/dev/null

GCMARGS256="-k a123456789012345678901234567890123456789012345678901234567890123456789012345678901234567 -g -t 16 -e 256"
echo  $0 ": starting GCM mode 256-bit (16 byte tag) rtpw receiver process... "

exec $RTPW $* $GCMARGS256 -r 127.0.0.1 $DEST_PORT &

receiver_pid=$!

echo $0 ": receiver PID = $receiver_pid"

sleep 1 

# verify that the background job is running
ps -e | grep -q $receiver_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 254
fi

echo  $0 ": starting GCM 256-bit (16 byte tag) rtpw sender process..."

exec $RTPW $* $GCMARGS256 -s 127.0.0.1 $DEST_PORT  &

sender_pid=$!

echo $0 ": sender PID = $sender_pid"

# verify that the background job is running
ps -e | grep -q $sender_pid
retval=$?
echo $retval
if [ $retval != 0 ]; then
    echo $0 ": error"
    exit 255
fi

sleep $DURATION

kill $receiver_pid
kill $sender_pid

wait $receiver_pid 2>/dev/null
wait $sender_pid 2>/dev/null

echo $0 ": done (test passed)"

else 

echo "error: can't find executable" $RTPW
exit 1

fi

# EOF


