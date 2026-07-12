#!/usr/bin/env bash

#
# usage: check-input.sh input_data
#

set -e	# stop on error
set -u	# uinitialized variables -> error!

#make

C_RED='\033[0;31m' # RED
C_GRN='\033[0;32m' # RED
C_NOC='\033[0m' # No Color

echo "Fuzzer Input: $1"
echo "########## Beginning Fuzzer Chain"
echo ""

set +e
./fuzzer_listen_verbose -timeout=10 $1 > $1.log 2>&1
#./fuzzer_connect_multi_verbose -timeout=10 $1 > $1.log 2>&1
FUZZER_RETVAL=$?
set -e

echo "Fuzzer returncode: $FUZZER_RETVAL"

if [ "$FUZZER_RETVAL" -eq "0" ]; then
	echo -e "$C_RED"
	echo "$1 - NOT REPRODUCABLE"
	echo -e "$C_NOC"
	exit $FUZZER_RETVAL
elif [ "$FUZZER_RETVAL" -eq "77" ]; then
	echo -e "$C_GRN"
	echo "$1 - REPRODUCABLE"
	echo -e "$C_NOC"
else
	echo "Unexpected return code: $FUZZER_RETVAL - handle with care..!"
	#exit
fi

grep "# SCTP_PACKET" $1.log > $1.pcap-log
text2pcap -n -l 248 -D -t "%H:%M:%S." $1.pcap-log $1.pcapng
rm $1.pcap-log

echo ""
echo "LOG:   $1.log"
echo "PCAP:  $1.pcapng"
echo ""

# Open Wireshark if we have an X session
#if [ -z ${DISPLAY+x} ]; then
	#echo "\$DISPLAY unset, skipping wireshark"
#else
	#wireshark $1.pcapng
#fi

exit $FUZZER_RETVAL
