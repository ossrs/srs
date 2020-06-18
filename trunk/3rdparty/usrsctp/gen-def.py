#!/usr/bin/env python3
#
# gen-def.py usrsctp.lib
import re
import sys
import subprocess
from shutil import which

try:
    lib_file = sys.argv[1]
except:
    print('Usage: gen-def.py LIB-FILE')
    exit(-1)

print('EXPORTS')

if which('dumpbin'):
    dumpbin_cmd = subprocess.run(['dumpbin', '/linkermember:1', lib_file],
        stdout=subprocess.PIPE)

    pattern = re.compile('\s*[0-9a-fA-F]+ _?(?P<functionname>usrsctp_[^\s]*)')

    for line in dumpbin_cmd.stdout.decode('utf-8').splitlines():
        match = pattern.match(line)
        if match:
            print(match.group('functionname'))
