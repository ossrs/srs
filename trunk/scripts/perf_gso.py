#!/usr/bin/python
'''
The MIT License (MIT)

Copyright (c) 2013-2016 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
'''

import urllib, sys, json

url = "http://localhost:1985/api/v1/perf"
if len(sys.argv) > 1:
    url = sys.argv[1]
print "Open %s"%(url)

f = urllib.urlopen(url)
s = f.read()
f.close()
print "Repsonse %s"%(s)

obj = json.loads(s)
keys = [1, 3, 5, 9, 16, 32, 64, 256, 1000]

print ""
print("AV---Frames"),
for k in keys:
    k2 = 'lt_%s'%(k)
    if 'frames' in obj['data']:
        p = obj['data']['frames']['msgs']
    else:
        p = obj['data']['writev']['msgs']
    if k2 in p:
        print(p[k2]),
    else:
        print(0),

print ""
print("RTP-Packets"),
for k in keys:
    k2 = 'lt_%s'%(k)
    if 'frames' in obj['data']:
        p = obj['data']['frames']['iovs']
    else:
        p = obj['data']['writev']['iovs']
    if k2 in p:
        print(p[k2]),
    else:
        print(0),

