---
name: internal-perf-for-srs
description: Use for SRS performance work - when asked to load test, benchmark or profile SRS or its Go proxy, find or fix a performance bottleneck, compare performance between versions or changes, or run or resume a performance round. Use from the srs-develop Task Router for performance tasks.
---

# SRS Performance Rounds

A round measures SRS under many simulated clients, finds the bottlenecks, improves them one by one, and ends with a report:

- **Round file:** each round has one file `cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-round.md` in this skill's own format, made from [references/round-template.md](references/round-template.md). It holds the round's data, the ranking, one chain per bottleneck, the steps and the work log. Like the reports, it is the round's working output, git-ignored and never committed. It is not an srs-autopilot task file: it has no worktree, tracker row or review table, and only this skill's workflows run it.
- **Reports:** the baseline report, and at the end an after-report, made from [references/report-template.md](references/report-template.md) in the git-ignored `cmd/perf/out/reports/`. They are not committed: their numbers hold only for the setup that made them. The round file links them.
- **Profiles:** [references/reading-profiles.md](references/reading-profiles.md) explains the flame graph, the symbol table, the syscall counts and how to rank, and the proxy's Go profiles.

## Skill Dependencies

- `skills/srs-develop/SKILL.md` — every code change, its tests and its commit.

## Rules

- **Paths:** `scripts/` and `references/` resolve relative to the directory containing this `SKILL.md`, not the current working directory; repository paths such as `cmd/perf/` and `bin/` resolve relative to the repository root.
- **By hand:** a round starts only when the user asks. An SRS version bump in `srs-develop` asks the user whether to run one for the new version; start it only on a yes. Never from a commit, a PR, CI or a schedule. Nothing here is a pass or fail gate.
- **One at a time:** one scenario, one step, one profile. A run takes minutes, about 10 for RTMP, and a round as long as its tries need. Keep the ramp steps and holds at their usual length, so a run compares with the baseline.
- **Alone on the machine:** load skews other jobs and they skew the numbers. `perf-run.sh` waits for and holds the `srs-develop` Full-tier lock for the whole run, so it never runs beside a Full tier.
- **Linux in Docker:** the numbers are fair for profiles and for comparing two runs on the same setup, not as the capacity of a real server. Every report says so.
- **Flat CBR source:** SRS CPU follows the media bitrate, so a VBR source makes two runs, or two servers at different points of the loop, not comparable. Every measurement, on every protocol, uses the flat CBR source in `srs-perf:local`:
  - `/srs/perf/cbr.flv`: 60s, H.264 at 200 kbps CBR with a one-frame buffer, 25 fps, an IDR every 2s, no B frames, and AAC 32 kbps.
  - `/srs/perf/cbr-flatness.json`: its flatness, which every report records. The image fails to build when the source is not flat.
  - Not `doc/source.flv`: its video runs from a few kbps to over 1 Mbps in one second.
  - `bin/srs-perf run` and `smoke` read it by default; `-media <flv>` names another source, which needs its `<name>-flatness.json` beside it. They refuse a source whose flatness record is missing or not flat, and copy the source and its record into each result's setup. `compare` treats two sources as two setups.
  - `make perf-env-check` publishes it too; the containers mount no media.
- **Per-second samples:** `bin/srs-perf run` and `smoke` sample once a second and keep every sample in `result.json`:
  - SRS CPU is the SRS container's cgroup `cpu.stat` `usage_usec`, which counts every thread to the microsecond. The load container runs with `--cgroupns host` and finds the cgroup from `/proc/<srs pid>/cgroup`; each sample keeps the raw `srs_cpu_usage_usec` too.
  - RSS is from `/proc/<srs pid>/status`, and each worker's CPU from its `/proc/<pid>/stat`.
  - Each step has a `window` with the mean and sd of each metric (SRS CPU, RSS, the workers' average CPU, frames, bytes, lag, failures) over its counted samples; a hold's result carries its step's `window`.
  - Anything else in the SRS container, such as `perf record`, counts in its cgroup too, so no profiler runs in a measuring window.
- **Lag in publisher time:** a player fails when its newest message is more than 2s behind the publisher's newest:
  - SRS's default `time_jitter full` starts each player's timestamps near zero, so a player that joins after the first IDR gets timestamps about one GOP smaller than the publisher's, though it is live.
  - The load tool places a player's first video frame in the source by its payload, so every video frame of the source must have a unique payload; a run refuses a source without one.
- **No profiler in the window:** a measuring hold lets its clients settle 30s, then counts every sample of its window, 60s by default and 180s for a verdict, with nothing else running in the SRS container:
  - `perf record` and `perf trace` run afterwards, in their own profile hold at the same client count, one per hold count.
  - A profile hold's CPU includes perf, so it is never a measurement: its result has no CPU per client, stays out of `run.json`, and `compare` refuses it.
  - The ramp only probes capacity by health. Its CPU per client includes connecting each step's new clients, so `compare` prints it as a note and never flags it.
- **A/B preferred, sequential accepted:** a comparison of two SRS builds reports each side's SRS CPU mean and sd over its window, and the difference with its 95% CI:
  - **A/B, preferred:** [scripts/perf-ab.sh](scripts/perf-ab.sh) runs both builds at once on their own cores, with the same source, load tool and client count, and pairs their samples per second. Both servers swing with the machine together, so the paired difference removes that shared noise.
  - **Sequential, accepted:** a run of the candidate compared with one recorded earlier under the same conditions: the source, cores, client counts and window. The CI is unpaired and noisier; say so wherever the result is written.
  - A verdict on a change uses a 180s window; both sides need the same window.
- **Temporary images:** only `srs-perf:local`, which every `make perf` rebuilds, is kept:
  - A baseline image `srs-perf-ab:<task>-<rev>` is removed by `perf-ab.sh` when it finishes, or with `docker rmi` once its comparison is done when it was kept with `-keep`.
  - A build-check image `srs-perf-build:<flag>` is removed when its check ends.
  - Remove every temporary image and container before a step ends, and log the images left. Ask the user before `docker builder prune`.
- **Shipped defaults:** SRS runs with `-e`, configured only by environment variables, one env file per scenario in `cmd/perf/env/<scenario>.env`, changed only where the scenario needs it. No config file. A tuning setting is a candidate like any other.

## Tools

- **`make perf`:** builds the image `srs-perf:local` and `bin/srs-perf`.
- **`make perf-unit`:** runs the load tool's Go unit tests.
- **`make perf-smoke SCENARIO=<s>`:** a short run of 100 clients of one scenario, to check that it works.
- **`make perf-smoke`:** With no `SCENARIO`, `make perf-smoke` smokes every scenario in turn and fails if any failed; about 5 minutes.
- **`make perf-source-test`:** checks that the CBR source in the image is flat.
- **`make perf-build-check`:** checks that the gperftools (`--gperf=on --gcp=on --gmp=on`), gprof (`--gprof=on`) and valgrind (`--valgrind=on`) builds still configure, build and start SRS; writes `build-check.json`.
- **`make perf-env-check`:** checks the containers, `perf` symbols and the core calibration.
- **`make perf-up` and `make perf-down`:** start and stop the SRS and load containers.
- **`bin/srs-perf compare <old.json> <new.json>`:** compares two results and flags a 10% change. For two holds, it gives each side's mean and sd and the 95% CI of the difference: paired per second for the two sides of one A/B run (`-ab-run <id>`), otherwise unpaired, which is noisier. The 10% flag uses the mean difference.

Scenarios. Each has its SRS env in `cmd/perf/env/<scenario>.env`. Unless noted, it is one RTMP publisher of the CBR source and N players:

- `rtmp-rtmp`: RTMP players.
- `rtmp-flv`: HTTP-FLV players.
- `rtmp-ts`: HTTP-TS players.
- `rtmp-rtsp`: RTSP players.
- `rtmp-hls`: HLS players.
- `rtmp-dash`: DASH players.
- `rtmp-whep`: WHEP players.
- `srt-srt`: an SRT publisher and SRT players.
- `whip-whep`: a WHIP publisher and WHEP players.
- **Cross-protocol scenarios:** publish in one protocol and play in another, through an SRS bridge:
  - `srt-rtmp`: SRT to RTMP.
  - `srt-whep`: SRT to WHEP.
  - `whip-rtmp`: WHIP to RTMP. SRS starts the stream only after two sender reports of each track, and takes each track's rate from the last two, so the WHIP publisher sends its own sender reports on the pace's clock, none from pion.
  - `whip-flv`: WHIP to HTTP-FLV, with `guess_has_av` off, or players get an audio-only FLV header.
  - SRS has no other bridge.
- `rtmp-pub`: N RTMP publishers, no player.
- `whip-pub`: N WHIP publishers, no player. Each publisher holds about 2.5 MB of SRS RSS and as much in the load tool, so at its 2,000 cap the holds run the 8 GB Docker VM out of memory: run it as `perf-run.sh -scenario whip-pub -cap 1000`.
- `many-streams`: N streams, each with one RTMP publisher and two RTMP players.
- `delays`: the delay probes of C7, not capacity. It measures the RTMP pause and unpause time (until `NetStream.Pause.Notify`), and the lag added by a forward target (`delays-forward.env`) and an edge (`delays-edge.env`).

Scripts, called by the workflows below:

- [scripts/perf-run.sh](scripts/perf-run.sh): the load run. A ramp to capacity in 5s steps, up to 40,000 clients (2,000 for SRT and WebRTC), three measuring holds (50%, 50% again for the noise, 90%), each a 30s settle and a `-window <seconds>`, then two profile holds into `profile-<50|90>/`. It prints the path of `run.json`.
- [scripts/perf-ab.sh](scripts/perf-ab.sh): the A/B run of two SRS builds at once, the preferred comparison. The baseline is `-baseline <rev>`, or HEAD with `-revert <commit>`, that commit's trunk change taken out; the candidate is the worktree. It builds `srs-perf-ab:<task>-<rev>`, removed unless `-keep`, and writes `ab.json` and `verdict.txt`.
- [scripts/perf-ab-test.sh](scripts/perf-ab-test.sh): tests the A/B run with HEAD against itself.
- [scripts/perf-profile.sh](scripts/perf-profile.sh): profiles one hold.
- [scripts/perf-source.sh](scripts/perf-source.sh): encodes the CBR source.
- [scripts/perf-source-check.sh](scripts/perf-source-check.sh) `<flv> <target kbps>`: checks that a source is flat.
- [scripts/perf-source-test.sh](scripts/perf-source-test.sh): the test behind `make perf-source-test`.
- [scripts/perf-env-check.sh](scripts/perf-env-check.sh): the environment check.
- [scripts/perf-build-check.sh](scripts/perf-build-check.sh): the check behind `make perf-build-check`, each build in its own image from `cmd/perf/Dockerfile.build`.
- [scripts/perf-build-check-test.sh](scripts/perf-build-check-test.sh): tests the build check.
- [scripts/perf-cap-check.sh](scripts/perf-cap-check.sh): one 20s hold at the client cap, to check that every player connects.
- [scripts/perf-verify.sh](scripts/perf-verify.sh) `<run dir>`: checks a run before ranking, and prints its top SRS symbols and noise.
- [scripts/perf-verify-test.sh](scripts/perf-verify-test.sh): tests the run check.
- [scripts/perf-skill-test.sh](scripts/perf-skill-test.sh): checks this skill's text and scripts.

Outputs go to the git-ignored `cmd/perf/out/<run>/`: `run.json`, each result's `result.json` and noise, and the profiles in `profile-<50|90>/`.

## Proxy

The Go proxy (`cmd/proxy`, `internal/proxy`) forwards bytes between clients and SRS origins; it never transmuxes. It is measured with the same tools, rules and workflows, with `-target proxy`; this section says what differs from SRS.

- **`perf-up.sh -target proxy -origins <1-4>`:** starts the proxy on vCPU 1 with `GOMAXPROCS=1` and Go pprof on loopback, and its SRS origins on vCPUs 2 to N+1, registered; three by default.
- **Several origins:** a capacity run uses three origins, each with a share of the players or streams, so the proxy runs out before an origin does; an A/B keeps one origin per side, since two proxies with three origins each do not fit in the VM's 10 vCPUs, so its hold count stays below the one-origin bound.
- **`make perf-smoke TARGET=proxy`:** smokes every proxy scenario, or one with `SCENARIO=<s>`.
- **[scripts/perf-proxy-scenarios.sh](scripts/perf-proxy-scenarios.sh):** the proxy scenarios and their origin counts.
- **`bin/srs-perf run -target proxy`:** samples the proxy and each origin's CPU; a player scenario publishes one stream on each origin and splits its players over them; an origin at 90% first ends the ramp as `origin-bound`.
- **`perf-run.sh -target proxy`:** runs a proxy scenario on its three origins, or N with `-origins <1-4>`, with Go profiles in the profile holds.
- **`perf-verify.sh` on a proxy run:** checks the Go profiles and each hold's shares on several origins, and prints the proxy's and origins' CPU, the bound that stopped the ramp and the top proxy symbols.
- **[scripts/perf-profile-go.sh](scripts/perf-profile-go.sh):** the Go profiles of one hold: CPU, allocations, mutex, block, trace, heap, goroutines and GC counts.
- **`perf-ab.sh -target proxy`:** the A/B of two proxies, with `-revert <commit>` that commit's proxy change taken out; each side on its own cores.
- **Report:** `<YYYY-MM-DD>-v<proxy version>-proxy-docker.md` and its JSON, from the same template, with the proxy rows of its Setup, the origin CPU beside the proxy's and the Go symbols in its Bottlenecks; the after-report is `-proxy-after-docker.md`, and a baseline on several origins `-proxy-origins-docker.md`, with the one-origin capacity beside each scenario's capacity on N origins, the bound and each origin's CPU.
- **[scripts/perf-proxy-up-test.sh](scripts/perf-proxy-up-test.sh):** tests the proxy containers.
- **[scripts/perf-profile-go-test.sh](scripts/perf-profile-go-test.sh):** tests the Go profiles on a short proxy hold.
- **`perf-ab-test.sh -target proxy`:** tests the proxy A/B.

### Proxy Topology

`perf-up.sh -target proxy -origins <1-4>` starts it, and `perf-down.sh` stops it:

- **Proxy:** `/srs/bin/srs-proxy` from `srs-perf:local`, from `cmd/perf/env/proxy/proxy.env`. It listens on SRS's default ports, so the load tool's clients are the same as for SRS, and uses the memory load balancer.
- **Go pprof:** `GO_PPROF=127.0.0.1:6060`, on loopback only. Mutex and block sampling are off by default; `/debug/pprof/rates?mutex=<n>&block=<ns>` turns them on, and `/debug/pprof/gc` answers the GC count and pause total.
- **Origins:** `SRS_PERF_ORIGINS` overrides the default count; origin i is on vCPU i+1. Each gets the scenario's env, then `origin.env` or `origin2.env` to `origin4.env`, which only moves its ports (RTMP 19351, HTTP 8081, API 19851, RTC 8001, SRT 10081; origin i adds 10 to the RTMP and API ports and 1 to the others, such as 19381, 8084, 19881, 8004, 10084 for the fourth) and registers with the proxy's system API at 12025 by heartbeat.
- **Load:** the rest of the cores, N+2 to 9. All containers share the proxy's network and pid namespaces, so the load tool sees the proxy as pid 1 and the origins beside it.

### Proxy Scenarios

Every protocol the proxy relays without a conversion, which would cost the origin and not the proxy:

- **Play:** `rtmp-rtmp`, `rtmp-flv`, `rtmp-ts`, `rtmp-hls`, `srt-srt`, `whip-whep`.
- **Publish:** `rtmp-pub`, `whip-pub`.
- **Many streams:** `many-streams`.
- **Not through the proxy:** RTSP and DASH have no proxy route, the cross-protocol scenarios are conversions, `rtmp-whep` makes the origin transcode AAC to Opus, and `delays` probes SRS's forward and edge.

### Measuring the Proxy

`perf-run.sh -target proxy -scenario <s>` runs the same ramp, measuring holds and profile holds as for SRS:

- **The subject is the proxy:** a sample's `srs_cpu_percent` and `srs_rss_bytes` are the proxy's, from its container's cgroup, and `origin_cpu_percent` holds each origin's CPU beside it. The setup records `target`, `proxy_version` and `origins`.
- **Streams over the origins:** the proxy picks a random origin for each new stream, so each step of a player scenario finds its streams in each origin's `/api/v1/streams`, and publishes again under a new name when a stream lands on an origin that has one. The publisher scenarios and `many-streams` stay at random. Each step's `shares`, and the setup's for the last step, give each origin's streams and players.
- **Why three origins:** on one origin, `rtmp-ts` stopped at 3,200 at its origin's limit, and on three it reached 6,400. A run still bound by an origin or the load reruns once with `-origins 4`; more cores or memory are not added.
- **The ramp stops** at the proxy's 90%, or as `origin-bound` when an origin reaches 90% first. An origin-bound scenario's capacity is the origin's, a lower bound of the proxy's; compare the proxy by its CPU per client at the holds.
- **compare** refuses a proxy result against an SRS one, and two results with different origin counts.

### Proxy Profiles

`perf-run.sh -target proxy` runs `scripts/perf-profile-go.sh` in each profile hold, from the load container, which shares the proxy's loopback. It writes to `profile-<50|90>/`:

- `cpu.pb.gz`, `allocs.pb.gz`, `mutex.pb.gz` and `block.pb.gz`, recorded together for 60s, with mutex and block sampling on only then.
- `trace.out`, a 10s runtime trace after them, so its cost stays out of the CPU profile.
- `heap.pb.gz` and `goroutine.txt`, snapshots at the end.
- `gc.json`, the GC count and pause total before and after.
- `top-symbols.txt` (`pprof -top` of the CPU profile) and `flamegraph.svg`.

`perf-verify.sh <run dir>` checks every file above in both profile holds, the proxy's own `srsx/` symbols and each origin's CPU at the holds, and prints them with the goroutine total and the GC counts.

No Linux `perf` and no syscall counts: the trace shows the syscalls. Read them per the Go profiles section of [references/reading-profiles.md](references/reading-profiles.md). The image has `pprof` but no Go toolchain, so open `trace.out` with `go tool trace` on the host.

### Proxy A/B

`perf-ab.sh -target proxy -baseline <rev> -clients <n> -scenario <s>`, or `-revert <commit>`:

- The baseline image is `srs-perf:local` with only the proxy rebuilt from the rev's `go.mod`, `go.sum`, `cmd/proxy` and `internal` (`cmd/perf/Dockerfile.ab-proxy`), so the origins and the load tool are the candidate's.
- Each side has its own proxy, origin and load cores: 1, 2 and 5–6 for the baseline, 3, 4 and 7–8 for the candidate, so one origin per side, `many-streams` too.
- So a verdict's hold count stays below the one-origin bound, such as the 90% count of a one-origin baseline, or the origin would bound both sides and hide the proxy's change.
- A verdict uses the 180s window, as for SRS. Compare two CPU profiles of the same hold count with `pprof -base <before cpu.pb.gz> <after cpu.pb.gz>`.

### Manual Proxy Profiling

When the containers cannot run, measure by hand on one Linux or macOS host. The numbers are then not comparable with a round's, so use it only to find a hot spot:

1. Build with `make`, and start the proxy with the env of `cmd/perf/env/proxy/proxy.env`, `GOMAXPROCS=1` and `GO_PPROF=127.0.0.1:6060`, its static files moved to the checkout's: `env $(grep -v '^#' cmd/perf/env/proxy/proxy.env | xargs) PROXY_STATIC_FILES=trunk/research ./bin/srs-proxy`.
2. Start an SRS origin from `trunk` with the scenario's env and `cmd/perf/env/proxy/origin.env`, such as `env $(cat ../cmd/perf/env/whip-whep.env ../cmd/perf/env/proxy/origin.env | grep -v '^#' | xargs) ./objs/srs -e`. For WebRTC, set `SRS_RTC_SERVER_CANDIDATE` to an address the players can reach; the proxy rewrites only the candidate's port.
3. Publish the flat CBR source, made by `bash scripts/perf-source.sh <cbr.flv>`: `ffmpeg -stream_loop -1 -re -i <cbr.flv> -c copy -f flv rtmp://127.0.0.1/live/livestream`, and start the players, such as `bin/srs-perf` or `srs_bench`. Check one player first; let them settle 30s.
4. Profile while the load is steady: `go tool pprof -top 'http://127.0.0.1:6060/debug/pprof/profile?seconds=60'`, and the same for `allocs`, `heap` and `goroutine?debug=1`; `curl -o trace.out 'http://127.0.0.1:6060/debug/pprof/trace?seconds=10'` and `go tool trace trace.out`.
5. Save each CPU profile, and diff a change's before and after with `go tool pprof -http=:8123 -base cpu-before.pb.gz cpu-after.pb.gz`; use a free port, not the proxy's 8080.

## Verified Proxy Scenarios

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

## Workflows

### Start a Round

1. Name it from the start date and the SRS version being measured, such as `cmd/perf/out/reports/2026-10-19-v8-round.md`, or `2026-10-19-v8-rtmp-round.md` for one protocol.
2. Copy [references/round-template.md](references/round-template.md) there, and fill every `<...>` placeholder: the version, the date, the checkout and branch the round works in (the current one unless the user names another), the scenarios, and the limit on improvements and tries if the user gave one, such as one kept change in at most 3 tries.
3. When a baseline report already exists for the round's commit and setup, such as one made while building the tools, start from it instead of measuring again:
   - Point Background's Baseline at the report and its JSON, and tick the Baseline steps with the run and report they came from.
   - Fill the Ranking from the report's Bottlenecks, and Chain 1 for the top one, with its baseline row: capacity, CPU per client and SRS CPU at both holds, and the top symbols with their shares.
   - Name the hold client counts of the baseline in Chain 1, since every fix is measured at the same counts.
4. **All protocols:** a round on every protocol takes every scenario of the registry in `cmd/perf/main.go`, and keeps every row of the template's Scenarios table:
   - The baseline is one `perf-run.sh` per scenario, about 10 minutes each, so about 3 hours for all of them. Run `whip-pub` with `-cap 1000`; `delays` is a probe for the fixed-delay candidates, with no capacity or profile.
   - Rank across the scenarios by the SRS code path, one chain per code path, and name in the chain every scenario the path serves, such as SRTP protect for `rtmp-whep`, `whip-whep` and `srt-whep`.
   - A fix measures each scenario its chain names, so a gain on one protocol is never a loss on another.
   - For one protocol, keep only its rows of the Scenarios table and delete the rest.
5. **A proxy round:** the Go proxy is measured on its own, never in an SRS round:
   - Name it from the proxy's version, `SRSX` in its `/api/v1/versions`, such as `2026-10-20-v8.0.59-proxy-round.md`, and set the template's Target line to `proxy`.
   - Keep the template's `## Proxy scenarios` table in place of its Scenarios table: every row of [Verified Proxy Scenarios](#verified-proxy-scenarios), with its origins.
   - The baseline is one `perf-run.sh -target proxy -scenario <s>` per proxy scenario, on its three origins, about 10 minutes each, so about 90 minutes for nine. Run `whip-pub` with `-cap 1000` and `rtmp-ts` with `-cap 6400`, both capped by the Docker VM's memory. Check each run with `perf-verify.sh`, and rank by the proxy's code path from the Go profiles. A run still bound by an origin or the load reruns once with `-origins 4`.
   - A fix's verdict is `perf-ab.sh -target proxy` at the 90% hold count of each scenario its code path serves, such as the RTMP relay for `rtmp-rtmp`, `rtmp-pub` and `many-streams`. The A/B runs one origin per side, so take a hold count below that scenario's one-origin capacity. Its tests are the proxy's, per [Check](#check).
6. The round file is in the git-ignored `cmd/perf/out/reports/`, so it is never committed.

### Build the Tools

1. Run `make perf`.
2. Run `make perf-env-check` when the image, Docker or the machine changed since the last round. If `perf` gets no samples or no SRS symbols, stop and ask the user; do not build `perf`, switch to a VM, or use another profiler on your own.

### Run the Load and Profile

1. Run `bash scripts/perf-run.sh -scenario <s>`:
   - One run, `-repeat 1`, is the default for a baseline and for a fix; about 10 minutes for RTMP.
   - The holds follow the run's own ramp. For a fix, `-hold-clients <50%>,<90%>` holds at the baseline's counts instead, such as `-hold-clients 6400,11520`, so the holds compare at the same load.
   - `-window <seconds>` sets the measuring holds' window: 60 by default, 180 for a fix's verdict. A comparison needs the same window on both sides.
   - Other flags pass through to `bin/srs-perf run` for the ramp, such as `-clients` and `-cap`.
2. Record in the round file the run directory, the capacity, each hold's SRS CPU mean and sd and its CPU per client, the noise between the two 50% holds, and the core calibration gap. A gap over 10% marks the run noisy. The run does not measure the gap itself: it copies the one from the last `make perf-env-check`, so note that check's date.
3. Rerun only when a comparison's 95% CI spans the 10% line, such as a −9% difference with a CI of −12% to −6%: run an A/B, or run again with a 180s `-window`, and log why.

### Rank Bottlenecks

1. Run `bash scripts/perf-verify.sh <run dir>`. Fix or rerun a run it fails before ranking it. It prints each profile hold's top SRS symbols and the noise between the two 50% holds; a noise of 10% or more is noted, so compare that scenario by an A/B or a 180s rerun. A delays run has no capacity or profile: the check needs each probe healthy with samples of every delay, and prints the delays. For a proxy run, it checks the Go profiles instead of `perf`'s, prints the proxy's and origins' CPU at each hold, the goroutines, the GC counts and the top proxy symbols, and marks an `origin-bound` capacity.
2. Read each profile hold's `top-symbols.txt`, `syscalls.txt` and flame graph per [references/reading-profiles.md](references/reading-profiles.md). A proxy run has Go profiles instead, read per its Go profiles section.
3. Rank by cost at the 90% profile hold, grouped into the SRS or proxy code path that owns it.
4. Write the ranking table in the round file: the rank, the code path, its self share at both holds, the syscall counts, and the SRS files and functions.
5. Start a chain for the new top bottleneck if it has none.

### Fix the Top Bottleneck

1. Take the current #1 of the ranking. Read its numbers and profile from the round file.
2. Change the SRS code per `srs-develop`.
3. Run [Check](#check).
4. Measure:
   - **The verdict:** an A/B at the 90% hold count with the default 180s window, such as `perf-ab.sh -baseline HEAD -clients 11520` before the commit, or `-revert <commit>` after it. Its `verdict.txt` gives each side's SRS CPU mean and sd and the paired difference with its 95% CI.
   - **Capacity and the other scenarios:** [Run the Load and Profile](#run-the-load-and-profile) for each scenario the change targets and each scenario its code path also serves, with `-hold-clients` at the baseline's hold counts, then `bin/srs-perf compare <baseline.json> <run.json>` for each. This is a sequential comparison: its CI is unpaired and noisier.
5. Decide:
   - **Keep** when the tests pass and no measured scenario loses 10% or more in capacity or CPU per client, even when the numbers are flat. A gain may show only after the rest of a bottleneck spread over several functions is fixed.
   - **Drop** when a test fails or a scenario loses 10% or more: restore the changed files with `git checkout -- <files>`, commit nothing, and log the numbers and both symbol tables.
   - A loss counts by the mean difference; when its 95% CI spans the 10% line, rerun per [Run the Load and Profile](#run-the-load-and-profile) before dropping.
6. Add the step to the bottleneck's chain: the change, the commit, the method and window, SRS CPU before and after as mean ± sd, the difference with its 95% CI, the capacity, and the top symbols with their shares.
7. Remove the A/B's baseline image if it was kept (`-keep`), and log the images left.
8. After a kept change, re-profile and [rank](#rank-bottlenecks) again; the next try fixes the new #1.
9. A chain with 5 kept steps and no gain overall is marked "no gain yet, needs review", is not reverted, and the round moves on to the next bottleneck.
10. The bottleneck work ends when the top bottleneck is something SRS cannot reasonably fix, such as the kernel, syscalls already batched, or crypto in RTC, or when the round's limit is reached.

### Check

- After the change, all the tests: the `srs-develop` Full tier. Every layer must pass.
- `make perf-unit` when `cmd/perf` changed.
- A change only to the Go proxy: the proxy's Go unit tests (`make test`), `proxy-utest.sh`, and every proxy E2E script of `srs-develop` (`proxy-e2e-test.sh` and the other `proxy-e2e-*-test.sh`), in place of the Full tier.

### Commit

Per the `srs-develop` commit format, one commit per kept change, on the round's branch; never push. Add the commit to its chain step. The message gives:

- The scenario, the client counts of the holds, and the run directories.
- The comparison's method and window, such as an A/B paired per second over 180s, or sequential and unpaired.
- SRS CPU before and after as mean ± sd, and the difference in points and percent with its 95% CI.
- The before and after capacity and CPU per client, and the `compare` flags.
- The top symbols with their self shares, before and after, from the profile holds.

### Write the Report

1. Make `cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-docker.md` from [references/report-template.md](references/report-template.md), and copy the run's combined JSON next to it with the same name and `.json`; a proxy report is `<YYYY-MM-DD>-v<proxy version>-proxy-docker.md`. Both are git-ignored and stay on this machine, for the round and `bin/srs-perf compare`.
2. The baseline report fills every section except Before and after:
   - The Setup's Source is the result's `setup.source` and its flatness record `setup.source_flatness`: the per-second mean, min and max, the per-frame sd, and the ffmpeg version.
   - Each hold gives SRS CPU as mean ± sd over its window, from the result's `window`.
3. The after-report fills Before and after too, with `bin/srs-perf compare` against the baseline, the method and 95% CI of each comparison, and the list of the round's commits:
   - Its final run uses `-hold-clients` at the baseline's hold counts, so every hold has a partner in the compare.
   - When no version bump came between them, it has the baseline's date and version; name it `<YYYY-MM-DD>-v<version>-after-docker.md`, with its JSON beside it, and link the baseline report from Before and after. A proxy after-report is `<YYYY-MM-DD>-v<proxy version>-proxy-after-docker.md`.
4. Link it from the round file's Background.
5. Flame graphs and `perf.data` stay in `cmd/perf/out/`; copy only the top-symbol tables into the report.

### Review

The user reviews the round's commits, listed in the chains, asynchronously while the round runs. Looking at the chains, the review keeps the commits that helped and drops the rest; the round needs no approval to go on.
