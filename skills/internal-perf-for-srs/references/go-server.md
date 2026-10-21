# Go Server Performance

The Go server is the code in `cmd/` and `internal/`. Today it is the proxy (`cmd/proxy`, `internal/proxy`), which forwards bytes between clients and C++ SRS origins and never transmuxes. Every tool measures it with `-target proxy`, with the same shared rules, tools and workflows of [SKILL.md](../SKILL.md); this file adds what is the Go server's own.

## Tools

- **`perf-up.sh -target proxy -origins <1-4>`:** starts the proxy on vCPU 1 with `GOMAXPROCS=1` and Go pprof on loopback, and its SRS origins on vCPUs 2 to N+1, registered; three by default.
- **Several origins:** a capacity run uses three origins, each with a share of the players or streams, so the proxy runs out before an origin does; an A/B keeps one origin per side, since two proxies with three origins each do not fit in the VM's 10 vCPUs, so its hold count stays below the one-origin bound.
- **`make perf-smoke TARGET=proxy`:** smokes every proxy scenario, or one with `SCENARIO=<s>`.
- **[scripts/perf-proxy-scenarios.sh](../scripts/perf-proxy-scenarios.sh):** the proxy scenarios and their origin counts.
- **`bin/srs-perf run -target proxy`:** samples the proxy and each origin's CPU; a player scenario publishes one stream on each origin and splits its players over them; an origin at 90% first ends the ramp as `origin-bound`.
- **`perf-run.sh -target proxy`:** runs a proxy scenario on its three origins, or N with `-origins <1-4>`, with Go profiles in the profile holds.
- **`perf-verify.sh` on a proxy run:** checks the Go profiles and each hold's shares on several origins, and prints the proxy's and origins' CPU, the bound that stopped the ramp and the top proxy symbols.
- **[scripts/perf-profile-go.sh](../scripts/perf-profile-go.sh):** the Go profiles of one hold: CPU, allocations, mutex, block, trace, heap, goroutines and GC counts.
- **`perf-ab.sh -target proxy`:** the A/B of two proxies, with `-revert <commit>` that commit's proxy change taken out; each side on its own cores.
- **Report:** `<YYYY-MM-DD>-v<proxy version>-proxy-docker.md` and its JSON, from the same template, with the proxy rows of its Setup, the origin CPU beside the proxy's and the Go symbols in its Bottlenecks; the after-report is `-proxy-after-docker.md`, and a baseline on several origins `-proxy-origins-docker.md`, with the one-origin capacity beside each scenario's capacity on N origins, the bound and each origin's CPU.
- **[scripts/perf-proxy-up-test.sh](../scripts/perf-proxy-up-test.sh):** tests the proxy containers.
- **[scripts/perf-profile-go-test.sh](../scripts/perf-profile-go-test.sh):** tests the Go profiles on a short proxy hold.
- **`perf-ab-test.sh -target proxy`:** tests the proxy A/B.

## Topology

`perf-up.sh -target proxy -origins <1-4>` starts it, and `perf-down.sh` stops it:

- **Proxy:** `/srs/bin/srs-proxy` from `srs-perf:local`, from `cmd/perf/env/proxy/proxy.env`. It listens on SRS's default ports, so the load tool's clients are the same as for SRS, and uses the memory load balancer.
- **Go pprof:** `GO_PPROF=127.0.0.1:6060`, on loopback only. Mutex and block sampling are off by default; `/debug/pprof/rates?mutex=<n>&block=<ns>` turns them on, and `/debug/pprof/gc` answers the GC count and pause total.
- **Origins:** `SRS_PERF_ORIGINS` overrides the default count; origin i is on vCPU i+1. Each gets the scenario's env, then `origin.env` or `origin2.env` to `origin4.env`, which only moves its ports (RTMP 19351, HTTP 8081, API 19851, RTC 8001, SRT 10081; origin i adds 10 to the RTMP and API ports and 1 to the others, such as 19381, 8084, 19881, 8004, 10084 for the fourth) and registers with the proxy's system API at 12025 by heartbeat.
- **Load:** the rest of the cores, N+2 to 9. All containers share the proxy's network and pid namespaces, so the load tool sees the proxy as pid 1 and the origins beside it.

## Scenarios

Every protocol the proxy relays without a conversion, which would cost the origin and not the proxy:

- **Play:** `rtmp-rtmp`, `rtmp-flv`, `rtmp-ts`, `rtmp-hls`, `srt-srt`, `whip-whep`.
- **Publish:** `rtmp-pub`, `whip-pub`.
- **Many streams:** `many-streams`.
- **Not through the proxy:** RTSP and DASH have no proxy route, the cross-protocol scenarios are conversions, `rtmp-whep` makes the origin transcode AAC to Opus, and `delays` probes SRS's forward and edge.

## Measuring

`perf-run.sh -target proxy -scenario <s>` runs the same ramp, measuring holds and profile holds as for the C++ server:

- **The subject is the proxy:** a sample's `srs_cpu_percent` and `srs_rss_bytes` are the proxy's, from its container's cgroup, and `origin_cpu_percent` holds each origin's CPU beside it. The setup records `target`, `proxy_version` and `origins`.
- **Streams over the origins:** the proxy picks a random origin for each new stream, so each step of a player scenario finds its streams in each origin's `/api/v1/streams`, and publishes again under a new name when a stream lands on an origin that has one. The publisher scenarios and `many-streams` stay at random. Each step's `shares`, and the setup's for the last step, give each origin's streams and players.
- **Why three origins:** on one origin, `rtmp-ts` stopped at 3,200 at its origin's limit, and on three it reached 6,400. A run still bound by an origin or the load reruns once with `-origins 4`; more cores or memory are not added.
- **The ramp stops** at the proxy's 90%, or as `origin-bound` when an origin reaches 90% first. An origin-bound scenario's capacity is the origin's, a lower bound of the proxy's; compare the proxy by its CPU per client at the holds.
- **compare** refuses a proxy result against an SRS one, and two results with different origin counts.

## Profiles

`perf-run.sh -target proxy` runs `scripts/perf-profile-go.sh` in each profile hold, from the load container, which shares the proxy's loopback. It writes to `profile-<50|90>/`:

- `cpu.pb.gz`, `allocs.pb.gz`, `mutex.pb.gz` and `block.pb.gz`, recorded together for 60s, with mutex and block sampling on only then.
- `trace.out`, a 10s runtime trace after them, so its cost stays out of the CPU profile.
- `heap.pb.gz` and `goroutine.txt`, snapshots at the end.
- `gc.json`, the GC count and pause total before and after.
- `top-symbols.txt` (`pprof -top` of the CPU profile) and `flamegraph.svg`.

`perf-verify.sh <run dir>` checks every file above in both profile holds, the proxy's own `srsx/` symbols and each origin's CPU at the holds, and prints them with the goroutine total and the GC counts. For a proxy run, it checks the Go profiles instead of `perf`'s, and marks an `origin-bound` capacity.

No Linux `perf` and no syscall counts: the trace shows the syscalls. The image has `pprof` but no Go toolchain, so open `trace.out` with `go tool trace` on the host.

## Go profiles

Read the `.pb.gz` files with `pprof` in the image or `go tool pprof` on the host, and `trace.out` with `go tool trace` on the host. As for the C++ server, the profile hold's CPU is never a measurement; take the proxy's CPU from the measuring hold at the same client count.

- **CPU, flat and cum:** `top-symbols.txt` is `pprof -top cpu.pb.gz`:
  - `flat` is the time in the function itself, like `perf`'s self share; `cum` adds what it calls.
  - Its header's `Total samples` over `Duration` is the proxy's CPU while profiled, so a share is a share of that.
  - `pprof -top -cum cpu.pb.gz` finds the proxy's own code path, such as `srsx/internal/proxy.(*rtmpConnection).serve`, and `pprof -peek '<symbol>' cpu.pb.gz` shows who calls a runtime or syscall symbol, such as `internal/runtime/syscall.Syscall6` for each read and write.
  - Go's profiler records whole stacks, so `flamegraph.svg` has no bare boxes of lost callers.
- **Allocations per packet:** `allocs.pb.gz` covers the same 60s. `pprof -sample_index=alloc_objects -top allocs.pb.gz` gives the objects per function, `alloc_space` the bytes. Divide the objects by the messages or packets the proxy forwarded in those 60s, the clients times the source's frames per second (more RTP packets for WebRTC) times 60. One or more per packet is a forwarding path that allocates per packet, such as a new reader or header buffer per message.
- **Heap:** `pprof -sample_index=inuse_space -top heap.pb.gz` is the memory held at the end of the hold; divide by the clients for the memory per client, beside the hold's RSS.
- **Goroutines:** `goroutine.txt` starts with `goroutine profile: total <n>`, then each group of goroutines with its count and stack. Divide the total by the clients: each protocol has a fixed number per client, such as a reader and a writer per connection. A count that grows between the two profile holds by more than their clients is a leak.
- **GC share:** `gc.json` holds the GC count `num_gc` and pause total `pause_total_ns` before and after the profiles, about 72s apart; their difference gives the GCs per second and the stop-the-world pause share. The GC's CPU is larger than its pauses: its share is the `cum` of `runtime.gcBgMarkWorker` plus `runtime.gcAssistAlloc` in the CPU profile. Fewer allocations lower it.
- **Mutex and block:** `pprof -top mutex.pb.gz` and `block.pb.gz` give where goroutines waited on locks and on channels or I/O. With `GOMAXPROCS=1` lock contention is rare; long blocks in idle goroutines waiting for a client are normal.
- **Trace:** `go tool trace trace.out` opens the 10s runtime trace in a browser:
  - The **Scheduler latency** profile shows how long runnable goroutines waited for the proxy's one core; a long wait is a core already full.
  - The **Syscall** profile shows the time in syscalls by stack, which replaces `perf trace`'s counts; `go tool trace -pprof=syscall trace.out > syscall.pb` makes it a pprof profile.
  - The goroutine analysis counts each goroutine's time running, waiting and in syscalls.
- **Ranking:** as for the C++ server: charge `runtime` and syscall time to the proxy code that called it, group into the proxy's code paths in `srsx/internal/proxy` and the protocol packages below it, and rank by `cum` share times the proxy's CPU at the 90% measuring hold. Syscalls per packet and allocations per packet are changes the proxy can make; the kernel's own send and receive is not.
- **Report:** a proxy report, `<YYYY-MM-DD>-v<proxy version>-proxy-docker.md`, copies the top 30 lines of `top-symbols.txt` and the allocation, goroutine and GC numbers of each profile hold.

## A/B

`perf-ab.sh -target proxy -baseline <rev> -clients <n> -scenario <s>`, or `-revert <commit>`:

- The baseline image is `srs-perf:local` with only the proxy rebuilt from the rev's `go.mod`, `go.sum`, `cmd/proxy` and `internal` (`cmd/perf/Dockerfile.ab-proxy`), so the origins and the load tool are the candidate's.
- Each side has its own proxy, origin and load cores: 1, 2 and 5–6 for the baseline, 3, 4 and 7–8 for the candidate, so one origin per side, `many-streams` too.
- So a verdict's hold count stays below the one-origin bound, such as the 90% count of a one-origin baseline, or the origin would bound both sides and hide the proxy's change.
- A verdict uses the 180s window, as for the C++ server. Compare two CPU profiles of the same hold count with `pprof -base <before cpu.pb.gz> <after cpu.pb.gz>`.

## Manual Profiling

When the containers cannot run, measure by hand on one Linux or macOS host. The numbers are then not comparable with a round's, so use it only to find a hot spot:

1. Build with `make`, and start the proxy with the env of `cmd/perf/env/proxy/proxy.env`, `GOMAXPROCS=1` and `GO_PPROF=127.0.0.1:6060`, its static files moved to the checkout's: `env $(grep -v '^#' cmd/perf/env/proxy/proxy.env | xargs) PROXY_STATIC_FILES=trunk/research ./bin/srs-proxy`.
2. Start an SRS origin from `trunk` with the scenario's env and `cmd/perf/env/proxy/origin.env`, such as `env $(cat ../cmd/perf/env/whip-whep.env ../cmd/perf/env/proxy/origin.env | grep -v '^#' | xargs) ./objs/srs -e`. For WebRTC, set `SRS_RTC_SERVER_CANDIDATE` to an address the players can reach; the proxy rewrites only the candidate's port.
3. Publish the flat CBR source, made by `bash scripts/perf-source.sh <cbr.flv>`: `ffmpeg -stream_loop -1 -re -i <cbr.flv> -c copy -f flv rtmp://127.0.0.1/live/livestream`, and start the players, such as `bin/srs-perf` or `srs_bench`. Check one player first; let them settle 30s.
4. Profile while the load is steady: `go tool pprof -top 'http://127.0.0.1:6060/debug/pprof/profile?seconds=60'`, and the same for `allocs`, `heap` and `goroutine?debug=1`; `curl -o trace.out 'http://127.0.0.1:6060/debug/pprof/trace?seconds=10'` and `go tool trace trace.out`.
5. Save each CPU profile, and diff a change's before and after with `go tool pprof -http=:8123 -base cpu-before.pb.gz cpu-after.pb.gz`; use a free port, not the proxy's 8080.

## Verified Scenarios

Every proxy scenario was verified with the Go proxy 8.0.59 and SRS 8.0.62 origins in October 2026, on the same setup, after the first proxy round's one change to the RTMP relay, on one origin and then on three origins (`2026-10-20-v8.0.59-proxy-origins-docker.md`, local):

- One `perf-run.sh -target proxy` run passed `perf-verify.sh`: a capacity, every step and hold healthy, and the proxy's own `srsx/` symbols in each Go profile.
- The capacity on three origins is the proxy's own limit: each origin carries a third of the players or streams. On one origin some scenarios stopped at the origin's limit, a lower bound of the proxy's; a round measures its own.
- The bound is what stopped the three-origin ramp, as `perf-verify.sh` names it: `proxy` at its 90%, `cap` when the last step is the cap, `failure` when players failed with the proxy and every origin under 90%, or `origin` and `load`, which no run hit.
- The top bottleneck, at the three-origin 90% profile hold, is the proxy code path where a proxy round's ranking starts looking.

| Scenario | Capacity on one origin | Capacity on three origins | Bound | Top bottleneck |
|---|---|---|---|---|
| `rtmp-rtmp` | 6,400 | 6,400 | proxy, 12,800 at 99.8% | The RTMP relay: reading each message (`readMessagePayload`) and queueing it, with the allocations per message |
| `rtmp-flv` | 12,800, near the origin's bound | 12,800 | proxy, 25,600 at 99.9% | A byte copy (`httpFlvTsConnection.serveByBackend`, kernel `write`), with about 150 KB of RSS per player |
| `rtmp-ts` | 3,200, origin-bound | 6,400 | cap, `-cap 6400`: the 12,800 step fills the Docker VM's memory | One origin read per chunk of the chunked body (`httpFlvTsConnection.serveByBackend`) |
| `rtmp-hls` | 6,400 | 12,800 | failure, 25,600 at proxy 57% | A new origin connection for most requests (`hlsPlayStream.serveByBackend`, `http.DefaultClient`) |
| `srt-srt` | 1,280 | 1,280 | proxy, 2,000 at 100% | A new `SRTConnection` per client packet (`internal/proxy/srt.go`), then one `sendto` per packet |
| `whip-whep` | 2,000, the cap | 2,000 | cap, proxy 54% at 1,800 | One `sendto` per packet (`rtcConnection.HandlePacket`) |
| `rtmp-pub` | 1,280 | 1,280 | proxy, 2,560 at 100% | The RTMP relay's read side: each basic header through `encoding/binary.Read` (`readBasicHeader`) |
| `whip-pub` | 1,000 with `-cap 1000`, by memory | 1,000 | cap, `-cap 1000`, proxy 44% at 900 | One `write` per packet to the origin, and the client address string per packet (`handleClientUDP`) |
| `many-streams` | 1,280 streams | 1,280 streams | proxy, 2,560 at 98% | The RTMP relay's read side (`ReadMessage`) |

## Start a Round

The Go server is measured on its own, never in a C++ round:

- **Name:** from the proxy's version, `SRSX` in its `/api/v1/versions`, such as `2026-10-20-v8.0.59-proxy-round.md`, and set the template's Target line to `proxy`.
- **Scenarios:** keep the template's `## Proxy scenarios` table in place of its Scenarios table: every row of [Verified Scenarios](#verified-scenarios), with its origins.
- **Baseline:** one `perf-run.sh -target proxy -scenario <s>` per proxy scenario, on its three origins, about 10 minutes each, so about 90 minutes for nine. Run `whip-pub` with `-cap 1000` and `rtmp-ts` with `-cap 6400`, both capped by the Docker VM's memory. Check each run with `perf-verify.sh`, and rank by the proxy's code path from the Go profiles. A run still bound by an origin or the load reruns once with `-origins 4`.
- **A fix's verdict:** `perf-ab.sh -target proxy` at the 90% hold count of each scenario its code path serves, such as the RTMP relay for `rtmp-rtmp`, `rtmp-pub` and `many-streams`. The A/B runs one origin per side, so take a hold count below that scenario's one-origin capacity. Its tests are the proxy's, per [Check](#check).

## Check

- A change only to the Go server: its Go unit tests (`make test`), `proxy-utest.sh`, and every proxy E2E script of `srs-develop` (`proxy-e2e-test.sh` and the other `proxy-e2e-*-test.sh`), in place of the Full tier.
- A change that also touches `trunk/`: the `srs-develop` Full tier, which runs the proxy tests too.

## Report

- The baseline is `<YYYY-MM-DD>-v<proxy version>-proxy-docker.md`, the after-report `<YYYY-MM-DD>-v<proxy version>-proxy-after-docker.md`, and a baseline on several origins `-proxy-origins-docker.md`, each with its JSON beside it.
