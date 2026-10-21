# C++ Media Server Performance

The C++ media server is the SRS origin and edge server in `trunk/`, one process on one thread of ST coroutines. It is the default target: every tool runs it without a `-target` flag, or with `-target srs`. This file adds to the shared rules, tools and workflows of [SKILL.md](../SKILL.md) what is the C++ server's own.

## Tools

- **`make perf-up` and `make perf-down`:** start and stop the SRS and load containers of [Topology](#topology).
- **`make perf-env-check`:** asserts the containers' cores, `ulimit -n` and UDP buffers from inside, records 10s of `perf record` with SRS symbols, and measures the core calibration gap.
- **`make perf-build-check`:** checks that the gperftools (`--gperf=on --gcp=on --gmp=on`), gprof (`--gprof=on`) and valgrind (`--valgrind=on`) builds still configure, build and start SRS; writes `build-check.json`.
- **[scripts/perf-profile.sh](../scripts/perf-profile.sh):** profiles one hold with Linux `perf`: `perf record -g`, `perf trace -s` for the syscall counts, the symbol table and the flame graph.
- **[scripts/perf-capture.sh](../scripts/perf-capture.sh):** the env check's 10s `perf record` at 999 Hz, as in the profile holds.
- **[scripts/perf-build-check.sh](../scripts/perf-build-check.sh):** the check behind `make perf-build-check`, each build in its own image from `cmd/perf/Dockerfile.build`.
- **[scripts/perf-build-check-test.sh](../scripts/perf-build-check-test.sh):** tests the build check.
- **`perf-ab.sh -revert <commit>`:** that commit's trunk change taken out, so the baseline SRS is HEAD without it; see [A/B](#ab).
- **Report:** `<YYYY-MM-DD>-v<version>-docker.md` and its JSON, from the report template; the after-report is `-after-docker.md`.

## Topology

`make perf-up` starts it, and `make perf-down` stops it:

- **SRS:** `srs-perf-srs`, `./objs/srs -e` from `srs-perf:local` with the scenario's `cmd/perf/env/<scenario>.env`, on vCPU 1, since SRS is one thread. It runs `--privileged` for `perf`, with `--ulimit nofile=1048576` and the local port range 10000 to 65535, so 40,000 players fit on `127.0.0.1`; the UDP buffers `net.core.rmem_max`, `wmem_max`, `rmem_default` and `wmem_default` are 16 MB.
- **Load:** `srs-perf-load` on vCPUs 2–9, eight worker processes of `bin/srs-perf`. It shares the SRS container's pid and network namespaces, so it sees SRS as pid 1 and dials `127.0.0.1`, and runs with `--cgroupns host` to read SRS's cgroup.
- **Extra servers:** a scenario with `<scenario>-forward.env` or `<scenario>-edge.env`, such as `delays`, starts a forward target or an edge SRS in the SRS container's network namespace, on the load cores, so the measured SRS keeps its own core.
- **Core calibration:** the Mac's 10 vCPUs mix performance and efficiency cores, and macOS may move SRS's vCPU to a slower core when the load is busy. `make perf-env-check` times a fixed CPU loop on vCPU 1 with the load cores idle and then busy ([scripts/perf-cpu-loop.sh](../scripts/perf-cpu-loop.sh), [scripts/perf-cpu-busy.sh](../scripts/perf-cpu-busy.sh)); a gap over 10% marks a run noisy.

## Scenarios

Each has its SRS env in `cmd/perf/env/<scenario>.env`. Unless noted, it is one RTMP publisher of the CBR source and N players:

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

## Measuring

`perf-run.sh -scenario <s>` runs the ramp, measuring holds and profile holds of the shared tools:

- **The subject is SRS:** a sample's `srs_cpu_percent` is the SRS container's cgroup CPU, found from `/proc/1/cgroup`, and `srs_rss_bytes` its RSS from `/proc/1/status`. One SRS thread on one vCPU means 100% is the whole server.
- **The ramp** doubles the clients each 5s step, from 100 by default, from 10 for SRT and WebRTC players and the publisher scenarios, and from 10 streams for `many-streams`. It stops at SRS's 90%, a failed health check, or the cap in `cmd/perf/main.go`: 40,000 clients by default, 2,000 for SRT and WebRTC players and WHIP publishers, and 13,333 streams for `many-streams`. A failed step with the workers' average over 95% is `load-bound`, not an SRS limit.
- **Max connections:** every scenario env sets `max_connections` to admit the cap; [scripts/perf-cap-check.sh](../scripts/perf-cap-check.sh) checks that the load side admits it.
- **Connect bursts:** a doubling step can fail on the connect burst, not on SRS's steady cost, such as 104 of 6,400 RTMP players getting no frame at 39.5% SRS CPU. When the capacity reads a step lower than the CPU suggests, rerun with `-hold-clients` at the expected counts, and log it.
- **compare** refuses an SRS result against a proxy one.

## Profiles

`perf-run.sh` runs `scripts/perf-profile.sh` in each profile hold, from the load container, writing to `cmd/perf/out/<run>/repeat-<n>/profile-<50|90>/`. Linux `perf` starts once the hold's clients have settled 30s:

- **`flamegraph.svg`:** the stacks of the 60s `perf record -g` of the SRS process.
- **`top-symbols.txt`:** `perf report --no-children --sort symbol`, every symbol with its self share and its callers.
- **`syscalls.txt`:** 10s of syscall counts, taken right after the record, inside the same profile hold, from `perf trace -s`, or `strace -c -f` when `perf trace` does not work. The tracer and `perf record` add SRS CPU, so a profile hold's CPU is never a measurement; compare syscall counts only between profile holds.
- **`perf.data`:** the raw record, for `perf report` with other options.

`perf-verify.sh <run dir>` checks the SRS symbols in both profile holds, and prints the top ones with the noise. A delays run has no capacity or profile: the check needs each probe healthy with samples of every delay, and prints the delays.

When `perf` gets no samples or no SRS symbols, stop and ask the user; do not build `perf`, switch to a VM, or use another profiler on your own. There is no manual fallback: `perf` needs the privileged container.

## Linux perf profiles

The profile hold's own `result.json` has no CPU per client and no result window, and stays out of `run.json`: perf's CPU counts in SRS's cgroup, so take SRS CPU only from a measuring hold.

The share of a sample is a share of SRS's own CPU time. With the window mean of SRS CPU in the measuring hold at the same client count, from its `result.json` in `run.json`, it becomes an absolute cost: a symbol with 10% self share at 80% SRS CPU costs 8% of a core.

### Flame graph

- Each box is a function; its width is the time spent in it and everything it calls.
- The top edge of a stack is where the CPU was; a wide top edge is a hotspot.
- Read from the bottom: the coroutine entry (`_st_thread_main`), then the SRS cycle that owns the work, such as `SrsRecvThread::cycle` for a publisher or `SrsRtmpConn::do_playing` for a player.
- Open it in a browser to search a symbol and see its total share.
- A wide bare `srs` box with nothing above it is samples whose frame-pointer call chain came out empty. For RTMP, these are the players' send path (`SrsRtmpConn::do_playing`, `writev` and the kernel's TCP send). Their self shares in `top-symbols.txt` are right; only their callers are lost, so group them by their leaf symbols.

### Symbol table

- Each top-level line is a symbol and its **self share**: the time spent in the symbol itself, not in what it calls.
- `[.]` is user space, such as SRS, ST and libc; `[k]` is the kernel.
- The indented tree under a symbol lists its callers, with the share each caller adds. Use it to charge libc and kernel time, such as `malloc`, `memcpy` or `writev`, to the SRS code that called it.
- List only the top-level lines with `grep -E '^ +[0-9]+\.[0-9]+%' top-symbols.txt | head -30`.
- `_st_epoll_dispatch` under `_st_idle_thread_start` is ST waiting for and dispatching events; a large share at a low load is idle time, not work.
- `SrsCplxError::srs_assert` with a few percent self is the assert out of line in every `SrsBuffer` read and write, a cost per byte op, not a failure.

### Syscall counts

- Read the calls and the share of syscall time per syscall. Divide the calls by 10s and by the hold's clients to get calls per client per second.
- `writev` per player shows how well SRS batches messages into one write; more calls per client per second means smaller batches.
- `epoll_pwait` calls show how often SRS wakes; fixed timers show as a floor that does not grow with the clients.
- `read` calls that return `EAGAIN`, each followed by two `epoll_ctl`, are a coroutine reading before its data arrived; many of them show in `_st_epoll_dispatch`'s self share.
- Calls with no clear owner, such as many `getpid` or `write` calls, often come from logging; find the caller in the symbol table.

### Hotspot moved

A change can be real without raising the capacity, so compare the profiles at the **same client count**, not only the numbers:

- **Moved:** the target symbol's self share drops, and the shares of other symbols rise to fill the gap.
- **Cheaper:** the SRS CPU window mean at the same client count drops, measured in measuring holds or an A/B, never in the profile holds; its 95% CI says whether the drop is more than noise.
- **Flat:** neither. The change may still help once the rest of a bottleneck spread over several functions is fixed, so it is kept when its tests pass and no scenario loses 10% or more; the review decides.
- Record both symbol tables with their shares and the SRS CPU% before and after; no threshold decides whether a hotspot moved.

### Ranking

1. Take the 90% profile hold of the latest run; use the 50% profile hold to confirm the cost grows with the load.
2. Charge each libc and kernel symbol to its SRS caller from the caller tree, and group symbols into the SRS code path that owns them, such as packet copying in `SrsLiveConsumer::enqueue` or message sending in `SrsRtmpConn::do_playing`.
3. Rank the code paths by absolute cost: their total self share times the window mean of SRS CPU in the 90% measuring hold.
4. Name the SRS files and functions of each code path, and the syscalls it makes.
5. The top bottleneck is the #1 code path that SRS can reasonably change. The kernel, syscalls already batched, and crypto in RTC are not; libsrt's own cost in `srt-srt` belongs to libsrt, not SRS.

## A/B

`perf-ab.sh -baseline <rev> -clients <n> -scenario <s>`, or `-revert <commit>`:

- The baseline image `srs-perf-ab:<task>-<rev>` is built from `trunk/` only: `-baseline` archives `<rev>:trunk`, and `-revert` three-way merges the trunk trees to take the commit's trunk change out of HEAD's, so a commit that also changed the tools still reverts. The worktree is never touched.
- The baseline SRS is on vCPU 1 and the candidate on vCPU 2, each with its own load container on vCPUs 3–9, started together on the CBR source.
- **Side offset:** two A/A runs, HEAD against itself, differed by +1.04 points at 6,400 RTMP players and −0.64 at 12,800, both CIs excluding 0. So two sides of one A/B can differ by up to about a point that per-second pairing does not remove; read a difference near a point with that in mind.
- A verdict uses the 180s window. The hold count is the baseline's 90%, such as 11,520 RTMP players.

## Verified Scenarios

Every scenario was verified with SRS 8.0.62 in October 2026, on the CBR source, one `perf-run.sh` each, passing `perf-verify.sh`: a capacity, every step and hold healthy, and SRS symbols in each profile. `rtmp-rtmp` is the CBR baseline (`2026-10-19-v8.0.62-docker.md`, local), after the fan-out fix. The top bottleneck is at the 90% profile hold, where a round's ranking starts looking.

| Scenario | Capacity | Top bottleneck |
|---|---|---|
| `rtmp-rtmp` | 12,800 | The fan-out into each player's queue: `SrsLiveConsumer::enqueue` and below 27.6% inclusive at 11,520 (`SrsMediaPacket::copy` 12.0%); then `_st_epoll_dispatch` 17.0% self |
| `rtmp-flv` | 12,800 | `_st_epoll_dispatch` 9.9% self, then each player's own FLV mux (`SrsFlvTransmuxer::write_tags` 5.6%); 80k `epoll_ctl` and 40k `EAGAIN` reads in 10s |
| `rtmp-ts` | 3,200 | One `writev` per media message per player (`SrsLiveStream::streaming_send_messages`, no merged write): 22 per player per second, HTTP-FLV 2.3 |
| `rtmp-hls` | 12,800, load-bound stop | Segment serving in 4 KB read and write pairs (`SrsHttpFileServer::copy`, `SRS_HTTP_TS_SEND_BUFFER_SIZE` 4096); SRS self at most 1.4% |
| `rtmp-dash` | 6,400 | The same 4 KB file serving, plus per-request headers and `srs_getenv` |
| `rtmp-rtsp` | 3,200 | Two `write` calls per RTP packet (`SrsRtspTcpNetwork::write`, the 4-byte `$` header then the packet, `mw_msgs = 1`); `srs_assert` 3.6% |
| `srt-srt` | 1,280 | libsrt, not SRS: `__kernel_clock_gettime` 22.5%, `CUDTUnited::locateAcquireSocket` 4.1%; SRS symbols under 1%; RSS 2.5 GB at 640 players |
| `rtmp-whep` | 1,280 | SRTP protect: libsrtp's `bitvector_left_shift` 10.0% (SRS's 8192-packet replay window, `srs_app_rtc_dtls.cpp`), HMAC-SHA1 6.3%, AES-CTR 4.8%; one `sendto` per packet |
| `whip-whep` | 2,000, the cap | The same SRTP path: `bitvector_left_shift` 11.7%, SHA-1 7.1%, AES-CTR 4.8%; `srs_assert` 5.8% |
| `srt-rtmp` | 6,400 | The RTMP fan-out, as `rtmp-rtmp`; the SRT bridge's TS demux under 1% |
| `srt-whep` | 1,280 | SRTP protect, as `rtmp-whep`; the AAC to Opus transcode about 0.2% |
| `whip-rtmp` | 12,800 | `_st_epoll_dispatch` 18.1% self, then the RTMP fan-out; the Opus to AAC transcode under 0.2% |
| `whip-flv` | 12,800 | `_st_epoll_dispatch` 13.9% self, `SrsFlvTransmuxer::write_tags` 6.0% |
| `rtmp-pub` | 1,280 | `_st_epoll_dispatch` 27.5% self; half the publishers' reads `EAGAIN`, each with two `epoll_ctl` |
| `whip-pub` | 1,000 with `-cap 1000`, by memory | SRTP unprotect (SHA-1 7.3%), `cfree` 6.3%, `SrsRtcConnection::find_publisher` 5.5% self per packet, NACK checks about 4% |
| `many-streams` | 640 streams | `_st_epoll_dispatch` 50.3% self with the same `EAGAIN` reads; SRS symbols each under 1.2% |
| `delays` | probe, no capacity | Pause 0.3 ms; unpause 261 ms (the paused consumer's 500ms sleep); added lag: forward 221 ms, edge play 1 ms, edge publish 220 ms (the 500ms receive timeout) |

## Start a Round

- **Name:** from the SRS version, such as `2026-10-19-v8-round.md`; the template's Target line is `srs`.
- **All protocols:** a round on every protocol takes every scenario of the registry in `cmd/perf/main.go`, and keeps every row of the template's Scenarios table:
  - The baseline is one `perf-run.sh` per scenario, about 10 minutes each, so about 3 hours for all of them. Run `whip-pub` with `-cap 1000`; `delays` is a probe for the fixed-delay candidates, with no capacity or profile.
  - Rank across the scenarios by the SRS code path, one chain per code path, and name in the chain every scenario the path serves, such as SRTP protect for `rtmp-whep`, `whip-whep` and `srt-whep`.
  - A fix measures each scenario its chain names, so a gain on one protocol is never a loss on another.
- **One protocol:** keep only its rows of the Scenarios table and delete the rest, and the `## Proxy scenarios` table.
- **Later-round candidates:** the template's list, such as the profiling builds of `make perf-build-check` and the fixed delays that `delays` measures, is tried after the bottleneck chains end, each judged like a bottleneck fix.

## Check

- After the change, all the tests: the `srs-develop` Full tier.

## Report

- The baseline is `<YYYY-MM-DD>-v<version>-docker.md`, the after-report `<YYYY-MM-DD>-v<version>-after-docker.md`, each with its JSON beside it.
- Its Setup's SRS container is vCPU 1, privileged for `perf`, and the load container vCPUs 2–9; its Bottlenecks copy the top 30 lines of `top-symbols.txt`, and its Syscalls the counts of `syscalls.txt`, per profile hold.
