#!/usr/bin/env python3
import glob
import subprocess
import os
import re

reportdir = "reports/"

class bcolors:
	HEADER = '\033[95m'
	OKBLUE = '\033[94m'
	OKGREEN = '\033[92m'
	WARNING = '\033[93m'
	FAIL = '\033[91m'
	ENDC = '\033[0m'
	BOLD = '\033[1m'
	UNDERLINE = '\033[4m'


print("Testing crashfiles")

FNULL = open(os.devnull, "w")
crashfiles = []
crashfiles.extend(glob.glob("*"))
pattern = re.compile("^(leak-|timeout-|crash-)\w+$")

filecounter = 1

FNULL = open(os.devnull, 'w')

for filename in crashfiles:

	if not pattern.match(filename):
		continue

	fuzzer_retval = subprocess.call(["./check-input.sh", filename, "batchmode"], stdout=FNULL, stderr=subprocess.STDOUT)
	
	if fuzzer_retval == 0:
		print(bcolors.FAIL, "[", filecounter, "]", filename,"- not reproducable", bcolors.ENDC)
	else:
		print(bcolors.OKGREEN, "[", filecounter, "]", filename, "- reproducable", bcolors.ENDC)

	filecounter = filecounter + 1
