# How to Analyze WHEP Performance for the Proxy Server

This guide walks through profiling the Go proxy under a WHEP (WebRTC play) load.
The workload of interest is **one RTMP publisher + N WHEP players**, where N is
large enough to stress the proxy's UDP forwarding path (typically 300+).

When analyzing WHEP performance for the proxy, you should:

1. Set up the topology: proxy + SRS origin + publisher + WHEP load
2. Enable Go pprof on the proxy
3. Run the load and let it warm up
4. Collect CPU, allocation, heap, goroutine, and trace profiles
5. Read the profiles and identify hot spots
6. Save profiles to compare before and after a change

## Step 1: Build and Start the Proxy with pprof

The proxy reads `GO_PPROF` from the environment and, when set, exposes
`net/http/pprof` endpoints at that address. Use the same standard ports SRS
uses by default so the publisher and player commands stay unchanged.

```bash
cd ~/git/srs
make && env GO_PPROF=:6060 \
    PROXY_RTMP_SERVER=1935 PROXY_HTTP_SERVER=8080 \
    PROXY_HTTP_API=1985 PROXY_WEBRTC_SERVER=8000 PROXY_SRT_SERVER=10080 \
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory \
    ./bin/srs-proxy
```

> The pprof endpoints live under `http://localhost:6060/debug/pprof/`. The
> proxy registers them only because `internal/debug/pprof.go` blank-imports
> `net/http/pprof`. Without that import the endpoints return 404.

## Step 2: Start the SRS Origin on Alt Ports

`origin1-for-proxy.conf` runs SRS on non-standard ports (RTMP 19351, HTTP 8081,
API 19851, RTC 8001/udp, SRT 10081) so the proxy can sit on the defaults. SRS
auto-registers with the proxy's system API on startup.

Set `CANDIDATE` to a LAN-reachable IP so the SDP answer the proxy returns
points clients at an address they can route to. The proxy only rewrites the
candidate **port**; the IP comes from the origin's SDP.

```bash
ulimit -n 10000 && bash -c "cd ~/git/srs/trunk && \
    CANDIDATE=192.168.3.187 ./objs/srs -c conf/origin1-for-proxy.conf"
```

## Step 3: Run the WHEP Workload

In separate terminals, start the publisher and the WHEP load generator.

**Publisher (RTMP):**

```bash
cd ~/git/srs/trunk
ffmpeg -stream_loop -1 -re -i doc/source.200kbps.768x320.flv \
    -c copy -f flv -y rtmp://localhost/live/livestream
```

**WHEP players (use the LAN IP that matches `CANDIDATE`):**

```bash
cd ~/git/srs/trunk/3rdparty/srs-bench
./objs/srs_bench -sr webrtc://192.168.3.187/live/livestream -nn 300
```

Let the workload run for at least 30 seconds before sampling. Connection
setup churn dominates the first few seconds and will skew profiles taken
too early.

> Sanity-check with `-nn 1` first. If a single WHEP session does not play,
> the 300-player run is testing something other than steady-state forwarding.

## Step 4: Collect Profiles

Profiles must be collected **while the workload is steady**, not before or
after. The CPU profile is the single most useful starting point.

```bash
# CPU profile (30s sample) — interactive web UI on :8123
# Use :8123 (or any free port) because :8080 is the proxy's HTTP-FLV/HLS port.
go tool pprof -http=:8123 'http://localhost:6060/debug/pprof/profile?seconds=30'

# Allocation profile — GC pressure / per-packet allocations
go tool pprof -http=:8124 http://localhost:6060/debug/pprof/allocs

# Heap (live memory snapshot)
go tool pprof -http=:8125 http://localhost:6060/debug/pprof/heap

# Goroutine count + stack dump — look for goroutine explosion under load
curl -s 'http://localhost:6060/debug/pprof/goroutine?debug=1' | head -50

# Runtime trace (10s) — GC pauses, scheduler latency, syscall behavior
curl -s -o trace.out 'http://localhost:6060/debug/pprof/trace?seconds=10'
go tool trace trace.out
```

The web UI requires Graphviz for the Flame Graph and Graph views:

```bash
brew install graphviz   # macOS
```

If you cannot install Graphviz, the **Top** view in the web UI is HTML-only
and works without it. The CLI form is also unaffected:

```bash
go tool pprof 'http://localhost:6060/debug/pprof/profile?seconds=30'
(pprof) top20
(pprof) top20 -cum
(pprof) list <FunctionName>
```

## Step 5: Read the Profiles

Open the web UI and use the views in this order:

1. **Flame Graph** — visual hot path. Wide bars near the top are where time
   is spent. For 300-player WHEP the path should be dominated by
   `webRTCProxyServer.Run` and its UDP read/write children.
2. **Top** — sorted list by `flat` (self time) and `cum` (cumulative). The
   top 5–10 functions usually tell the whole story.
3. **Graph** — call graph with edge weights. Good for tracing "who calls this
   hot function".
4. **Source** — line-level cost inside a single function. Use after Top has
   pointed you at a function worth dissecting.

## Step 6: Save Profiles for Before/After Comparison

When you change code to fix a hot spot, comparing profiles is the only
reliable way to confirm the fix moved the needle (and didn't just shift cost
elsewhere).

```bash
# Save the raw profile from a baseline run
curl -s -o cpu-before.pb.gz 'http://localhost:6060/debug/pprof/profile?seconds=30'

# After the code change, sample again under the same workload
curl -s -o cpu-after.pb.gz 'http://localhost:6060/debug/pprof/profile?seconds=30'

# Diff the two
go tool pprof -http=:8123 -base cpu-before.pb.gz cpu-after.pb.gz
```

In the diff view, red bars are functions that got more expensive, green
bars are functions that got cheaper. The total should shrink overall if
the change is a net win.
