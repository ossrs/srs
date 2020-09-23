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
if len(sys.argv) < 2:
    print "Usage: %s <url>"%(sys.argv[0])
    print "For example:"
    print "     %s http://localhost:1985/api/v1/perf"%(sys.argv[0])
    sys.exit(-1)

url = sys.argv[1]
print "Open %s"%(url)

f = urllib.urlopen(url)
s = f.read()
f.close()
print "Repsonse %s"%(s)

obj = json.loads(s)

print ""
p = obj['data']['dropped']
print('Frame-Dropped: %.1f%s'%(10000.0 * p['rtc_dropeed'] / p['rtc_frames'], '%%'))
p = obj['data']['bytes']
print('Padding-Overload: %.1f%s %dMB'%(10000.0 * p['rtc_padding'] / p['rtc_bytes'], '%%', p['rtc_padding']/1024/1024))

# 2, 3, 5, 9, 16, 32, 64, 128, 256
keys = ['lt_2', 'lt_3', 'lt_5', 'lt_9', 'lt_16', 'lt_32', 'lt_64', 'lt_128', 'lt_256', 'gt_256']
print("\n----------- 1 2 [3,4] [5,8] [9,15] [16,31] [32,63] [64,127] [128,255] [256,+) Packets"),

print ""
print("AV---Frames"),
p = obj['data']['avframes']
for k in keys:
    k2 = '%s'%(k)
    if k2 in p:
        print(p[k2]),
    else:
        print(0),
print(p['nn']),

print ""
print("RTC--Frames"),
p = obj['data']['rtc']
for k in keys:
    k2 = '%s'%(k)
    if k2 in p:
        print(p[k2]),
    else:
        print(0),
print(p['nn']),

print ""
print("RTP-Packets"),
p = obj['data']['rtp']
for k in keys:
    k2 = '%s'%(k)
    if k2 in p:
        print(p[k2]),
    else:
        print(0),
print(p['nn']),

print ""
print("GSO-Packets"),
p = obj['data']['gso']
for k in keys:
    k2 = '%s'%(k)
    if k2 in p:
        print(p[k2]),
    else:
        print(0),
print(p['nn']),

