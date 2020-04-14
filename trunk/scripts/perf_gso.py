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
keys = [3, 5, 9, 16, 32, 64, 128, 256, 1000]

print ""
print("AVFrames"),
for k in keys:
    k2 = 'lt_%s'%(k)
    p = obj['data']['writev']['msgs']
    if k2 in p:
        print(p[k2]),
    else:
        print(0),

print ""
print("RTP-Packets"),
for k in keys:
    k2 = 'lt_%s'%(k)
    p = obj['data']['writev']['iovs']
    if k2 in p:
        print(p[k2]),
    else:
        print(0),

