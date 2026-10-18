# SRS Cross-Component Integration Tests

Use this suite for every standalone SRS runtime code change, whether the change is in the next-generation Go proxy or the C++ media server. Apply it during development, bug fixing, and pull-request review.

The `proxy-*` filenames describe the test entry point, not a proxy-only scope: the scripts exercise the proxy, SRS origin and edge roles, Redis routing, and RTMP, HTTP-FLV, HLS, SRT, and WHIP interoperability.

## Rules

- Run focused and component-native tests first, then run the commands below as in [Parallel Runs](#parallel-runs).
- Do not substitute unit tests, black-box tests, E2E protocol tests, or benchmarks for this suite.
- Do not skip a script merely because the edited file is outside the Go proxy.
- Continue after failures so every result is recorded; fix relevant failures and repeat the complete suite.

## Suite

Every script is in `skills/srs-develop/scripts/`; run it as `bash skills/srs-develop/scripts/<script>`. Run `make integ` and `make integ-unit` from the repository root.

| # | Script | Covers |
|---|---|---|
| 1 | `proxy-utest.sh --coverage` | Go proxy unit tests with coverage |
| 2 | `proxy-e2e-test.sh` | Single-origin RTMP proxy |
| 3 | `proxy-e2e-cluster-test.sh` | Multi-origin memory load-balancer routing |
| 4 | `proxy-e2e-edge-test.sh` | Proxy, SRS edge, and SRS origin three-tier topology with a late-joining player |
| 5 | `proxy-e2e-redis-test.sh` | Redis multi-proxy routing |
| 6 | `proxy-e2e-transmux-test.sh` | RTMP publish with RTMP, HTTP-FLV, HLS, and WHEP playback verification |
| 7 | `proxy-e2e-srt-test.sh` | SRT publish with SRT, RTMP, HTTP-FLV, HLS, and WHEP playback verification |
| 8 | `proxy-e2e-whip-test.sh` | WHIP publish with RTMP, HTTP-FLV, HLS, and WHEP playback verification |
| 9 | `proxy-e2e-bearer-auth-test.sh` | Bearer authentication startup validation, protected SRS, WHIP/WHEP, and proxy APIs, and authenticated origin registration |
| 10 | `srs-config-file-test.sh` | SRS started from a config file, because the scripts above start SRS with environment variables only (`-e`): RTMP publish with RTMP, HTTP-FLV, HLS, and WHEP playback, WHIP publish with WHEP and RTMP playback, and the config pid, log, and HLS paths |
| 11 | `make integ` | The black-box `srs-integ` suite in `cmd/integ`: every SRS protocol and feature from outside the process, as a client, an operator or a callback server sees it, against `trunk/objs/srs` |
| 12 | `make integ-unit` | The unit tests of `srs-integ` itself, every test in `cmd/integ` outside `cases/`: its media clients, stream analyzers, test media, hook mock and server adapter |

### Dependencies

- The SRT test requires an FFmpeg build with libsrt.
- The WHIP and config file tests require the `whip` muxer and OpenSSL.
- These scripts automatically run `skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh` on macOS when no suitable FFmpeg is available.
- The Redis test runs `brew services start redis` when Redis is not running; do not start a temporary `redis-server` instead.
- If an environmental dependency is unavailable, run the script, preserve its exact result, and report the blocked coverage instead of claiming full verification.
- The transmux, SRT, WHIP, and config file tests play WHEP with `tools/pion-whep`, because FFmpeg has no WHEP demuxer; they build it with `go` when the binary is missing or stale.
- `make integ` and `make integ-unit` first make the test media with FFmpeg (`make integ-prepare`), which needs libx264, libx265 and an Opus encoder.
- The OBS tests of lane G need OBS with obs-websocket and Python 3.9+, and the Linux run needs Docker. Without them they print a `SKIP` line, which the Full tier lists as unverified.

## Feature-Specific Tests

Run feature-specific bundled tests in addition to this matrix when the routed workflow or the touched area requires them. Every script is in `skills/srs-develop/scripts/`; run `.js` with `node` and `.sh` with `bash`.

| Script | Covers |
|---|---|
| `obs-rtmp-publish-test.sh` | Isolated OBS H.264/AAC RTMP publish: API active and 640×360, HTTP-FLV and HLS video/audio playback; owned cleanup and a 60 s budget. Requires OBS, Python 3.9+, FFmpeg, jq and curl; `SRS_TEST_OBS=0` skips. |
| `obs-srt-publish-test.sh` | Isolated OBS H.264/AAC SRT publish with the streamid in the Stream Key: SRT-to-RTMP bridge, API active and 640×360, HTTP-FLV video/audio playback; owned cleanup and a 60 s budget. Same dependencies and `SRS_TEST_OBS` gate as the RTMP test. |
| `obs-whip-publish-test.sh` | Isolated OBS H.264/Opus WHIP publish with `eip=127.0.0.1`: RTC-to-RTMP bridge, API active and 640×360, API frames `KNOWN F2` while they read 0, HTTP-FLV H.264/AAC playback; owned cleanup and a 60 s budget. Same dependencies and `SRS_TEST_OBS` gate as the RTMP test. |
| `obs-lane-test.sh` | The three OBS cases in parallel, each on its own slot from `SRS_TEST_PORT_BASE` (default 15100, then 15200 and 15300): their logs and results in order, then `LANE obs passed= skipped= known= failed= elapsed=`; fails over 60 s wall time. `SRS_TEST_OBS=0` skips. |
| `obs-windows-test.sh` | OBS on the Windows test host publishes to SRS in WSL there, through `windows-host-run.sh` (`--wip` for the working tree): WSL builds the commit at the same path as a worktree of its main repository, SRS runs env-only on slot 15900, and the RTMP H.264/AAC and WHIP H.264/Opus cases (WHIP with `eip=<WSL IP>`) run in parallel with the RTMP and WHIP checks; prints the build time and `RESULT windows`, and fails when the cases take over 60 s. `--build` stops after the WSL build. Opt in with `SRS_TEST_WINDOWS_HOST`; `SRS_TEST_OBS=0` skips. The host needs WSL with the SRS repository cloned at the same path, and Python, FFmpeg, jq and curl in Git Bash. |
| `obs-linux-test.sh` | OBS on this machine publishes to SRS built and run for Linux in Docker: the build step (`--build` alone) copies the tracked `trunk` files with their modification times into the volume `srs-obs-linux-build` and builds `objs/srs` there in `ossrs/srs:ubuntu20` with the Full-tier flags minus `--utest`, so the local `trunk/objs` keeps its own; SRS runs env-only in a container on slot 15400 with its ports published on `127.0.0.1`, and the RTMP H.264/AAC, SRT H.264/AAC and WHIP H.264/Opus cases (WHIP with `eip=127.0.0.1`) run in parallel with the same checks as the local cases; prints the build time and `RESULT linux`, fails when the cases take over 60 s, and removes the container on exit. Without a running Docker it prints `SKIP linux: unverified: Docker not available`; `SRS_TEST_OBS=0` skips. Same OBS dependencies as the RTMP test. |
| `browser-page-url-test.js` | Browser player page URL parsing, for `trunk/research/players` changes. |
| `gb28181-api-timeout-test.sh` | External-SIP GB28181 API session timeout cleanup. |
| `gb28181-tcp-disconnect-test.sh` | External-SIP GB28181 TCP disconnect cleanup. |
| `srs-reload-test.sh` | Config reload of a running SRS, for changes to reload, the RAW API or signal handling: reloads through `rpc=reload` and SIGHUP with a live RTMP publisher, a parse failure, a `check_config` failure and a `-e` server, each checked through `rpc=reload-fetch` and `rpc=raw`. Inotify auto-reload is Linux-only and not covered. |
| `install-target-test.sh` | The `make install` recipe generated by `trunk/configure`, for changes to that `install` target or to `trunk/etc/init.d/srs`. |
| `rtc-rtx-whip-publish-test.sh` | RFC 4588 RTX on the WebRTC publish path: FFmpeg publishes over WHIP under `nack_prefer_rtx on`, packets are dropped through the NACK simulator API, and SRS must unwrap RTX retransmissions. |
| `rtc-rtx-whep-play-test.sh` | RFC 4588 RTX on the WebRTC play path: `tools/pion-whep` plays over WHEP under `nack_prefer_rtx on`, sent packets are dropped, and SRS must answer the NACK on the RTX SSRC. |
| `rtc-plain-whep-play-test.sh` | Plain retransmission on the WebRTC play path, the RTX fallback: the same player under `nack_prefer_rtx off` must get an answer without `rtx` and a resend on the media SSRC. |
| `rtc-plain-whip-publish-test.sh` | Plain retransmission on the WebRTC publish path, the RTX fallback: `tools/pion-whip` publishes over WHIP under `nack_prefer_rtx off`, offering `rtx` with a FID group, and must get an answer without `rtx` and resend on the media SSRC. |
| `forward-status-test.sh` | `forward.destination` and the read-only `/api/v1/forwards/` API: an origin forwards to two destination SRS, and each forward is listed with its url and `forwarding`, `{id}` returns one, an unknown id fails and `DELETE` is 405, a stopped destination shows `retrying`, and after its restart it is `forwarding` again with the same id. |

Helper scripts such as `gb28181-create-session.sh`, `gb28181-publish-stream.sh`, `proxy-e2e-origin.sh`, and `setup-ffmpeg-with-whip.sh` are not standalone test cases unless a workflow explicitly invokes them.

## Adding srs-integ Cases

`cmd/integ/README.md` has the full rules and the catalog. A new case follows them:

- **Black box:** it observes SRS only through the network, the HTTP API, the callbacks, the files SRS writes, its exit code and the signals it gets, never its source or log text.
- **Name:** `Test<Feature>_<Group>/<Case>`, or `TestTimer_<Feature>_<Group>/<Case>` when it waits on an SRS timer. Reuse a feature name from the catalog.
- **Group:** `Common`, the shared server with every feature on, with a stream name of its own. A variant server only when Common can't do it: a value Common can't have, global state, a process change, or a topology.
- **Class:** declared first, with `check.Case`: Fast ≤100ms, Normal ≤500ms, or Timer ≤10s in `TestTimer_*` groups only. It checks only through `c`.
- **Publish:** in real time, like FFmpeg `-re`, except HLS, DASH and DVR cases, which publish without `-re`.
- **Env only:** SRS starts with `-e` and `SRS_*` env vars, never a config file. A feature that needs a file gets no case.
- **Known bugs:** a case that finds an SRS bug is marked `KnownBug(c, "<reason>")`, not fixed or skipped.
- **Protocols:** a new protocol gets the whole checklist: signaling, every codec, lifecycle, bad input and its options.
- **Catalog:** a new feature or group gets its row in the catalog in `cmd/integ/README.md`.
- **Run:** the case alone with `go -C cmd/integ/cases test -count=3 -run '<name>'`, then `make integ`, which stays within 60s.

## Verification Tiers

The trigger picks the tier, never the expected runtime. Report every layer not run as unverified.

| Tier | Trigger | Run |
|---|---|---|
| **Iterate** | After each small change | The focused test, plus all C++ and Go unit tests |
| **Gate** | Once per batch, before a commit that will be pushed or opened as a PR | Iterate, the suite above, and the black-box or regression cases for the touched protocol |
| **Full** | When the user asks to run all tests, before merging a PR, and before pushing a backport or release | Every layer below, with no subset |

### Full Tier Layers

Run every layer with `bash skills/srs-develop/scripts/srs-full-tier.sh`, after `full-tier-lock.sh status`. It runs them in the [Order](#order) below, under the [Global Lock](#global-lock), and prints one row per layer.

**C++ unit.** `make utest && ./objs/srs_utest`, configured as in [Parallel Runs](#parallel-runs).

**The suite above.**

**Black-box.** In `trunk/3rdparty/srs-bench`:

1. `make test`.
2. `./objs/srs_blackbox_test -test.v -test.run '^TestFast' -test.parallel 64`.
3. Then `-test.run '^TestSlow' -test.parallel 3`.

Pass `-srs-ffmpeg "$(command -v ffmpeg)" -srs-ffprobe "$(command -v ffprobe)"` to both runs, because its lookup ignores `PATH`.

**Regression**, which starts SRS from a config file like `srs-config-file-test.sh`. In `trunk/`:

1. Start SRS on the regression ports, from `SRS_TEST_PORT_BASE`, by default 13000, with its pid and log in `/tmp/srs-regression`:
   ```bash
   PORT_BASE=${SRS_TEST_PORT_BASE:-13000}
   mkdir -p /tmp/srs-regression
   env SRS_RTMP_LISTEN=$PORT_BASE SRS_HTTP_API_LISTEN=$((PORT_BASE + 1)) SRS_HTTP_SERVER_LISTEN=$((PORT_BASE + 2)) \
       SRS_RTC_SERVER_LISTEN=$((PORT_BASE + 3)) SRS_RTSP_SERVER_LISTEN=$((PORT_BASE + 4)) SRS_STREAM_CASTER_LISTEN=$((PORT_BASE + 5)) \
       SRS_STREAM_CASTER_OUTPUT="rtmp://127.0.0.1:$PORT_BASE/live/[stream]" \
       SRS_PID=/tmp/srs-regression/srs.pid SRS_SRS_LOG_FILE=/tmp/srs-regression/srs.log \
       ./objs/srs -c conf/regression-test.conf
   ```
2. Wait up to 10s until `curl -fs http://127.0.0.1:$((PORT_BASE + 1))/api/v1/versions` answers.
3. In `3rdparty/srs-bench`, run `./objs/srs_test -test.v -srs-rtmp-port $PORT_BASE -srs-api-port $((PORT_BASE + 1)) -srs-rtsp-port $((PORT_BASE + 4)) -srs-http-server 127.0.0.1:$((PORT_BASE + 2))` as four processes in parallel, one with each of:
   - `-test.run '^TestRtcDTLS_ClientPassive'`
   - `-test.run '^TestRtcDTLS_ClientActive'`
   - `-test.run '^TestRtx'`
   - `-test.skip '^(TestRtcDTLS_Client(Passive|Active)|TestRtx)'`
4. Kill `$(cat /tmp/srs-regression/srs.pid)`.

**Feature-specific.** Every feature-specific test listed above, with `SRS_GB_SKIP_BUILD=1` for the `gb28181-*-test.sh` scripts (their build reconfigures `trunk/`).

**OBS**, lane G:

1. `obs-lane-test.sh`: the RTMP, SRT and WHIP cases against the local `objs/srs`.
2. `obs-linux-test.sh`: the same cases against SRS for Linux in Docker.
3. `obs-windows-test.sh`, only with `SRS_TEST_WINDOWS_HOST`: RTMP and WHIP from OBS on the Windows test host to SRS in WSL there.

Two variables control it:

- `SRS_TEST_OBS`: `1` by default; `0` skips the OBS builds and lane G, and the summary says `lane G: skipped (SRS_TEST_OBS=0)`.
- `SRS_TEST_WINDOWS_HOST`: the Windows test host, as a placeholder `<srs-test-windows-host>`; unset, the Windows build and run are left out.

Check each layer's exit code separately, and strip ANSI codes before counting `--- PASS`. The OBS layers count their `PASS` and `FAIL` lines. Each OBS `SKIP` or `KNOWN` line is listed below the summary as unverified, never counted as passed.

`srs-full-tier-test.sh` tests the runner itself: the step order, lane G, both gates and the summary, with fake layers.

### Sub-agent

Run a Gate or Full tier in a sub-agent, so the build and test output stays out of the main agent's context. Give it the tier and the tree to test, and wait for its result instead of reading the logs.

The sub-agent runs the tier as in [Parallel Runs](#parallel-runs), keeps every log in a file, and returns only:

- The two lock lines.
- One row per layer or script: exit code, pass and fail counts, and duration.
- The unverified OBS lines, or that lane G was skipped.
- For each failure: the failing check, the few log lines that show its cause, and the log path.

To look into a failure, ask that sub-agent, or start a new one, rather than reading the logs. If the tool has no sub-agents, run the tier directly and report the same result.

## Parallel Runs

### Global Lock

Hold the global lock for the whole tier, so the tiers of other agents never overlap. In the one script that runs it:

```bash
# Before the first layer.
echo "lock: waiting, $(bash skills/srs-develop/scripts/full-tier-lock.sh status)"
start=$(date +%s)
token=$(bash skills/srs-develop/scripts/full-tier-lock.sh acquire --wait 600)
echo "lock: acquired after $(( $(date +%s) - start ))s"
# In an EXIT trap.
bash skills/srs-develop/scripts/full-tier-lock.sh release "$token"
```

- Log both lines in the tier's summary, and tell the user each one as it happens: `waiting, free` or `waiting, held: ...` with the holder, then `acquired after Ns`.
- Each product has its own lock, so the runs of different products go in parallel:
  - SRS: `/tmp/srs-full-tier.lock`, the default, held by the Full tier.
  - State Threads: `/tmp/srs-full-tier-st.lock`, with `--product st`, held by `st-test.sh`.
  - Windows test host: `/tmp/srs-full-tier-windows-host.lock`, with `--product windows-host`, held by `windows-host-run.sh` for each run on the host, which `windows-host-run-test.sh` tests. The owner's exit frees it; it is stale only after 3600s.
- One older than 300s, or whose script exited, is taken over.
- `full-tier-lock-test.sh` tests it.

### Order

Run the layers of every tier in this order.

**Step 1: build**, before every lane, one after another, because `./configure` rewrites `trunk/objs`, which every lane runs from:

1. Configure `trunk/` once with the `Dockerfile.test` flags without `--build-cache`; they include `--simulator=on`, which the `rtc-*` scripts and the NACK log unit tests need:
   ```bash
   ./configure --srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on
   ```
2. Build `objs/srs`. Every lane uses it, and none uses `srs_utest`.
3. Build the proxy and `bin/srs-integ` (`make`), and `tools/pion-whep`.

Beside these, unless `SRS_TEST_OBS=0`, the OBS builds run in parallel, since neither touches `trunk/objs`:

- `obs-linux-test.sh --build`: SRS for Linux in Docker, from the tracked files.
- `obs-windows-test.sh --build`, only with `SRS_TEST_WINDOWS_HOST`: SRS in WSL on the Windows test host.

**Step 2: test**, these lanes in parallel, each lane in its own order:

| Lane | Runs |
|---|---|
| A | The Go proxy unit tests, then the eight `proxy-e2e-*` scripts in parallel, each on its own default port slot |
| B | Black-box fast |
| C | Black-box slow |
| D | Regression |
| E | `srs-config-file-test.sh` and the feature-specific tests in parallel |
| F | `make utest`, then `./objs/srs_utest`. With `objs/srs` built, `make utest` does not relink it |
| G | The OBS scripts in parallel, the Linux run a second after the local cases; skipped with `SRS_TEST_OBS=0` |

**Step 3: lane H**, alone after Step 2: `make integ-unit`, then `make integ`. Its cases have time limits, and its timer cases measure SRS timers, which the load of the other lanes delays.

### Times

Measured on a 10-core arm64 Mac with `objs/srs` already built, on 2026-10-17, in seconds:

| Step or lane | `SRS_TEST_OBS=1`, two runs | `SRS_TEST_OBS=0` |
|---|---|---|
| Step 1 | 8, or 26 with the Windows build; the Linux build 4–5 | 5 |
| A | 31–36 | 28 |
| B | 26–27 | 24 |
| C | 32 | 31 |
| D | 19–21 | 17 |
| E | 31–34 | 31 |
| F | 16–20 | 13 |
| G | 28–45 | skipped |
| Step 2 wall | 36–45 | 31 |
| H | 21–36 | 21 |
| Total | 75–98 | 57 |

- **Lane G** must finish within 60 s. Its slow runs come from the one relaunch of an OBS instance that hangs in setup under load, which adds about 20 s to that case.
- **CPU:** in Step 2, lane G raises the Mac's mean CPU from 26% to 34–41%, and its peak from 72% to 100% for a few seconds.
- **Windows:** with `SRS_TEST_WINDOWS_HOST`, Step 1 waits for the host sync and the WSL build, about 20 s, of which an incremental build is about 5 s, and lane G runs the Windows cases in 22–24 s on the host.

## Port Plan

Each test process binds ports only from its own range, so the parallel lanes never collide.

- A new script or test takes its ports from its owner's range, or from the reserved range.
- Every range is a default that an environment variable moves without a code change, and none has to be set:
  - `SRS_TEST_PORT_BASE`, the first port of a script's slot or of the regression SRS.
  - `SRS_BLACKBOX_PORTS` and `SRS_UTEST_PORTS`, the range of an allocator, like `21000-25999`.
- No test binds a port below 11000, which is kept for human testing and shared services.
- No test binds a port from 32768, which covers the ephemeral ports of Linux (32768–60999), macOS and Windows (49152–65535): a client socket there makes a later bind fail.
- No test binds the Go proxy defaults 11935, 11985, 12025, 18000, 18080 and 20080 from `internal/env/env.go`, which are kept for running the proxy by hand.
- Update this table, including its counts, whenever a test adds, moves or removes a port.

| Range | Owner | Total ports | Bound today | Available for new tests |
|---|---|---|---|---|
| 1–10999 | System and shared: the defaults of a human SRS or Oryx, such as 1935, 1985, 8080, 8000, 8554, 9000 and 10080, and Redis on 6379 | 10,999 | 0 | 0, never bound by tests |
| 11000–11799 | The eight `proxy-e2e-*` scripts, one 100-port slot each at `SRS_TEST_PORT_BASE`, by default 11000 to 11700 in the order of the Suite | 800 | 99 | 701, inside the existing slots |
| 11800–11899 | Spare `proxy-e2e-*` slot | 100 | 0 | 100 |
| 11900–11999 | Kept free for the proxy defaults 11935 and 11985 | 100 | 0 | 0 |
| 12000–12099 | Kept free for the proxy default 12025 | 100 | 0 | 0 |
| 12100–12999 | The other bundled scripts, one 100-port slot each at `SRS_TEST_PORT_BASE`, by default 12100 `srs-config-file`, 12200 `srs-reload`, 12300 and 12400 `gb28181-*`, 12500 to 12800 `rtc-*`, 12900 `forward-status` | 900 | 31 | 869, inside the existing slots |
| 13000–13999 | The regression SRS at `SRS_TEST_PORT_BASE`, by default 13000, through the `SRS_*_LISTEN` env and the `srs_test` port flags | 1,000 | 6 | 994 |
| 14000–14999 | The `srs-integ` suite in `cmd/integ`, the main process from 14000–14799 and the timer process from 14800–14999, moved by `SRS_INTEG_PORTS` | 1,000 | Allocated per server | 0, owned by the allocator |
| 15000–15099 | Isolated OBS websocket allocation, moved by `OBS_CONTROL_WS_PORTS` | 100 | Random, per instance | 0, owned by the allocator |
| 15100–15199 | `obs-rtmp-publish-test.sh` at `SRS_TEST_PORT_BASE`: RTMP +0, HTTP +1, API +2, RTC UDP +3, SRT UDP +4 | 100 | 5 | 95, inside the existing slot |
| 15200–15299 | `obs-srt-publish-test.sh` at `SRS_TEST_PORT_BASE`: RTMP +0, HTTP +1, API +2, RTC UDP +3, SRT UDP +4 | 100 | 5 | 95, inside the existing slot |
| 15300–15399 | `obs-whip-publish-test.sh` at `SRS_TEST_PORT_BASE`: RTMP +0, HTTP +1, API +2, RTC UDP +3, SRT UDP +4 | 100 | 5 | 95, inside the existing slot |
| 15400–15499 | `obs-linux-test.sh` at `SRS_TEST_PORT_BASE`: published from its SRS container on `127.0.0.1`, RTMP +0, HTTP +1, API +2, RTC UDP +3, SRT UDP +4 | 100 | 5 | 95, inside the existing slot |
| 15500–15899 | Reserved for new test layers | 400 | 0 | 400 |
| 15900–15999 | `obs-windows-test.sh` on the Windows test host, not the local machine: SRS in WSL with RTMP +0, HTTP +1, API +2, RTC UDP +3; its OBS instances take websocket ports from 15000–15099 there | 100 | 4 | 96, inside the existing slot |
| 16000–20999 | Reserved for new test layers, except the proxy defaults 18000, 18080 and 20080 | 5,000 | 0 | 4,997 |
| 21000–25999 | Black-box SRS servers, from `SRSPortAllocator` in `trunk/3rdparty/srs-bench/blackbox`, moved by `SRS_BLACKBOX_PORTS` | 5,000 | Random, per test | 0, owned by the allocator |
| 26000–29999 | C++ unit tests, from `srs_utest_random_port()`, `_srs_tmp_port` and `_srs_tmp_srt_port`, moved by `SRS_UTEST_PORTS` | 4,000 | Random, per test | 0, owned by the allocator |
| 30000–32767 | Reserved for new test layers | 2,768 | 0 | 2,768 |
| 32768–65535 | System: the kernel's ephemeral ports for client sockets and port-0 binds | 32,768 | 0 | 0, never bound by tests |
| **Total** | | **65,535** | | |
