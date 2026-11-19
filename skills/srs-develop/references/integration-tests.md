# SRS Cross-Component Integration Tests

Use this suite for every standalone SRS runtime code change, whether the change is in the Go proxy or the C++ media server. Apply it during development, bug fixing, and pull-request review.

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
- The Chrome tests of lane I need Node.js 22.12+ and Chrome or Edge. Without them they print a `SKIP` line, which the Full tier lists as unverified.

## Feature-Specific Tests

Run feature-specific bundled tests in addition to this matrix when the routed workflow or the touched area requires them. Every script is in `skills/srs-develop/scripts/`; run `.js` with `node` and `.sh` with `bash`.

OBS tests:

- `obs-rtmp-publish-test.sh` — isolated OBS H.264/AAC RTMP publish.
  - Checks: API active and 640×360, HTTP-FLV and HLS video/audio playback.
  - Owned cleanup and a 60 s budget.
  - Requires OBS, Python 3.9+, FFmpeg, jq and curl; `SRS_TEST_OBS=0` skips.
- `obs-srt-publish-test.sh` — isolated OBS H.264/AAC SRT publish, with the streamid in the Stream Key.
  - Checks: the SRT-to-RTMP bridge, API active and 640×360, HTTP-FLV video/audio playback.
  - Owned cleanup and a 60 s budget.
  - Same dependencies and `SRS_TEST_OBS` gate as the RTMP test.
- `obs-whip-publish-test.sh` — isolated OBS H.264/Opus WHIP publish, with `eip=127.0.0.1`.
  - Checks: the RTC-to-RTMP bridge, API active and 640×360, API frames `KNOWN F2` while they read 0, HTTP-FLV H.264/AAC playback.
  - Owned cleanup and a 60 s budget.
  - Same dependencies and `SRS_TEST_OBS` gate as the RTMP test.
- `obs-lane-test.sh` — the three OBS cases in parallel.
  - Each case on its own slot from `SRS_TEST_PORT_BASE` (default 15100, then 15200 and 15300).
  - Prints their logs and results in order, then `LANE obs passed= skipped= known= failed= elapsed=`.
  - Fails over 60 s wall time; `SRS_TEST_OBS=0` skips.
- `obs-windows-test.sh` — OBS on the Windows test host publishes to the native `srs.exe` there.
  - Runs through `windows-host-run.sh`, with `--wip` for the working tree.
  - `srs-windows-runner.sh build` builds `srs.exe`, then `obs-lane-test.sh` runs the RTMP, SRT and WHIP cases there, with the same checks and slots as on this machine, each against its own `srs.exe`.
  - Prints the build time and the cases' lines, and fails when the cases take over 60 s.
  - `srs-windows-runner.sh obs` runs the same cases on the host after its earlier steps.
  - Opt in with `SRS_TEST_WINDOWS_HOST`; `SRS_TEST_OBS=0` skips.
  - The host needs OBS, and Python, FFmpeg, jq and curl in Git Bash.
- `obs-linux-test.sh` — OBS on this machine publishes to SRS built and run for Linux in Docker.
  - Build step (`--build` alone): copies the tracked `trunk` files with their modification times into the volume `srs-obs-linux-build`, and builds `objs/srs` there in `ossrs/srs:ubuntu20` with the Full-tier flags minus `--utest`, so the local `trunk/objs` keeps its own.
  - SRS runs env-only in a container on slot 15400, with its ports published on `127.0.0.1`.
  - The RTMP H.264/AAC, SRT H.264/AAC and WHIP H.264/Opus cases (WHIP with `eip=127.0.0.1`) run in parallel, with the same checks as the local cases.
  - Prints the build time and `RESULT linux`, fails when the cases take over 60 s, and removes the container on exit.
  - Without a running Docker it prints `SKIP linux: unverified: Docker not available`; `SRS_TEST_OBS=0` skips.
  - Same OBS dependencies as the RTMP test.

Chrome tests:

They drive Chrome with `skills/internal-chrome-control/SKILL.md`. Each case uses its own SRS, owns its cleanup, has a 60 s budget, and needs Node.js 22.12+ and Chrome or Edge; `SRS_TEST_CHROME=0` skips. A `KNOWN F<n>` line is a known SRS defect the case tolerates. Each script's header lists its checks.

- `chrome-webrtc-test.sh` — Chrome publishes H.264/Opus over WHIP and plays it back over WHEP and HTTP-FLV; it also plays FFmpeg's RTMP stream over WHEP.
- `chrome-loss-test.sh` — NACK under loss with the NACK simulator, downlink and uplink, with `nack_prefer_rtx` off and on. Needs `--simulator=on`.
- `chrome-http-test.sh` — Chrome plays FFmpeg's RTMP stream over HTTP-FLV, HTTP-TS and HLS.
- `chrome-codec-test.sh` — Chrome publishes and plays H.264, VP9, AV1 and H.265 over WebRTC; a codec the browser cannot encode is a `SKIP`.
- `chrome-sped-test.sh` — SPED with Chrome's field trial `WebRTC-IceHandshakeDtls`: Chrome publishes over WHIP and plays over WHEP on a `sped on` SRS, on the same SRS losing its first STUN response of each session (`drop-stun=1`), and on a `sped off` SRS, which also loses the DTLS 1.2 last flight of two sessions (`drop-finished=1`). Every job negotiates DTLS 1.3 when `/api/v1/features` reports `dtls13` (a `--ssl-4=on` build), else DTLS 1.2.
  - Checks: SPED complete in the clients API with Chrome's ClientHello embedded, and after the lost response SRS's first flight embedded too; no SPED with `sped off`; frames encoded and decoded. After the lost last flight, SRS answers Chrome's resend, and the script prints the gap, the ms from the loss to the resend.
  - A browser whose offer has no `sped` with the trial is a `SKIP`; without `--simulator=on` the drop and last parts are a `SKIP`, and on DTLS 1.3 the last part is.
- `chrome-lane-test.sh` — lane I: the unit tests below, then the first four cases in parallel, then SPED, whose two browsers beside them made the loss case's players freeze under the Full tier's load; `--cases-only` runs only the cases.
- `chrome-linux-test.sh` — the five cases against SRS for Linux in Docker, like `obs-linux-test.sh`; `--build` builds it.
- `chrome-windows-test.sh` — the five cases with Edge on the Windows test host, like `obs-windows-test.sh`.
- Unit tests: `npm test` in `skills/internal-chrome-control` for the driver, and `chrome-test-lib-test.sh` for the helper `chrome-test-lib.sh`.
- CI: the `chrome-test` job of `.github/workflows/test.yml` runs the unit tests and `chrome-linux-test.sh` with Chrome on Linux.

Windows tests:

- `srs-windows-runner.sh` — the native Windows build and tests of SRS on the Windows test host.
  - Use it for changes that reach the Windows build, such as `trunk/src`, `trunk/cmake/windows`, `trunk/vcpkg.json` or `cmd/integ`.
  - Run it from here with `windows-host-run.sh [--wip] <checkout> srs-windows-runner.sh [--alone] <step> [pattern]`.
  - Test steps, each running the steps before it; `--alone` runs only the given one:
    - `deps`: ST, the vcpkg libraries and FFmpeg-fit, with a link check each.
    - `build`: `srs.exe`, then one start that answers on RTMP, HTTP and the API.
    - `utest`: `srs_utest.exe`.
    - `integ [pattern]`: `cmd/integ` with `-integ.limit-scale 4`, the pattern to `go test -run`.
    - `obs` and `full`.
  - Other steps:
    - `vs`: after `deps`, generates the Visual Studio projects in `trunk/objs/windows-vs`, and writes the committed solution `.vs/srs.slnx` that opens them, to build, run and debug in the IDE; commit `.vs/srs.slnx` when git shows a change.
    - `package [version]`: for the release workflow, builds after `deps` and `build` without the test options, and zips `srs.exe`, `conf` and the http root as `trunk/objs/windows/package/SRS-Windows-x86_64-<version>.zip`.
  - Prints `RESULT windows-x64` lines, with the logs in `trunk/objs/windows-*.log` on the host.
  - It refuses to start off native Windows, WSL included.
  - Opt in with `SRS_TEST_WINDOWS_HOST`.
- `srs-windows-env-check.sh` — the Windows test host has every tool the runner needs, over SSH.
  - Visual Studio 2022 or its Build Tools with cl, CMake and vcpkg.
  - Git Bash with make, Go and FFmpeg; OBS; MSYS2 with make, nasm and pkgconf.
  - One `TOOL` or `MISSING` line per tool.
  - Run it to set up a host, or when the runner fails on a tool. Opt in with `SRS_TEST_WINDOWS_HOST`.

Other tests:

- `openssl3-api-check.sh` — the OpenSSL APIs SRS uses that OpenSSL 3 deprecates or removes, for changes to OpenSSL calls.
  - Compiles every source of a configured `trunk` syntax only against OpenSSL 3 headers, as is and with `OPENSSL_NO_DEPRECATED`, with clang or GCC.
  - A compile that fails before the source, such as a bad option, is an `ERROR`.
  - A list, not a gate: it exits 1 while any use is left.
- `browser-page-url-test.js` — browser player page URL parsing, for `trunk/research/players` changes.
- `gb28181-api-timeout-test.sh` — external-SIP GB28181 API session timeout cleanup.
- `gb28181-tcp-disconnect-test.sh` — external-SIP GB28181 TCP disconnect cleanup.
- `srs-reload-test.sh` — config reload of a running SRS, for changes to reload, the RAW API or signal handling.
  - Reloads through `rpc=reload` and SIGHUP with a live RTMP publisher, a parse failure, a `check_config` failure and a `-e` server.
  - Each is checked through `rpc=reload-fetch` and `rpc=raw`.
  - Inotify auto-reload is Linux-only and not covered.
- `install-target-test.sh` — the `make install` recipe generated by `trunk/configure`, for changes to that `install` target or to `trunk/etc/init.d/srs`.
- `forward-status-test.sh` — `forward.destination` and the read-only `/api/v1/forwards/` API.
  - An origin forwards to two destination SRS, and each forward is listed with its url and `forwarding`.
  - `{id}` returns one, an unknown id fails and `DELETE` is 405.
  - A stopped destination shows `retrying`, and after its restart it is `forwarding` again with the same id.

WebRTC retransmission tests:

- `rtc-rtx-whip-publish-test.sh` — RFC 4588 RTX on the WebRTC publish path.
  - FFmpeg publishes over WHIP under `nack_prefer_rtx on`, and packets are dropped through the NACK simulator API.
  - SRS must unwrap RTX retransmissions.
- `rtc-rtx-whep-play-test.sh` — RFC 4588 RTX on the WebRTC play path.
  - `tools/pion-whep` plays over WHEP under `nack_prefer_rtx on`, and sent packets are dropped.
  - SRS must answer the NACK on the RTX SSRC.
- `rtc-plain-whep-play-test.sh` — plain retransmission on the WebRTC play path, the RTX fallback.
  - The same player under `nack_prefer_rtx off` must get an answer without `rtx` and a resend on the media SSRC.
- `rtc-plain-whip-publish-test.sh` — plain retransmission on the WebRTC publish path, the RTX fallback.
  - `tools/pion-whip` publishes over WHIP under `nack_prefer_rtx off`, offering `rtx` with a FID group.
  - It must get an answer without `rtx` and a resend on the media SSRC.

WebRTC SPED tests:

- `go -C cmd/integ/cases test -count=3 -run 'TestRtc_Sped'` — SPED (`draft-hancke-webrtc-sped`, the DTLS handshake inside the ICE STUN checks), part of `make integ`.
  - The raw client of `cmd/integ/internal/rtc` offers `a=ice-options:sped` to a server with `vhost.rtc.sped on`, over UDP and over TCP.
  - Checks: `sped` in the answer, SPED complete on the client and in the clients API, the handshake datagrams embedded both ways, and the media plays.
  - Loss: a lost first response, a lost client flight, every raw DTLS datagram lost, and 25% random loss over 20 sessions.
  - `SimulatorLosesFirstResponse` has SRS lose its first response and raw flights itself, with `drop-stun=<n>` and `drop-dtls=<n>` in the WHIP or WHEP URL, which a `--simulator=on` build takes, as `/api/v1/features` reports with `handshake_drop`.
  - `SimulatorLosesLastFlight` and `TcpSimulatorLosesLastFlight` play without SPED, over UDP and TCP, with `drop-finished=1`, which loses SRS's DTLS 1.2 last flight, CCS and Finished: the client resends its own, SRS answers it again, and the media plays.
  - `PlayerStartsOnSpedComplete` and `TcpPlayerStartsOnSpedComplete` play a stream already being published, with SPED over UDP and TCP, losing SRS's DTLS 1.2 last flight raw and in its first response: no media comes before the player has that flight, then the media plays.
  - `SpedOffFallsBack` checks that the default `sped off` answers no `sped` and the client falls back to a raw handshake.
- `chrome-sped-test.sh` — the same with Chrome's own SPED, in lane I; see the Chrome tests above.
- `sped-measure.sh [measure flags]` — a measurement, not a test, in no tier: the setup time with and without SPED.
  - It starts `trunk/objs/srs` with `sped on` and runs `bin/srs-integ measure`, whose raw WHEP players offer `sped` or not over paths with a one-way delay and a loss of STUN and DTLS.
  - It prints one row per point: the p50 and p95 in ms from the offer to the DTLS handshake done and to the first RTP packet, and the failures. The default matrix is 0/20/60/150 ms, 0/10/25% loss, 30 runs each.
- `chrome-sped-measure.sh [--runs <n>]` — a measurement, not a test, in no tier: Chrome's setup time with and without its SPED field trial, at zero loss, and the DTLS version negotiated.
  - It starts `trunk/objs/srs` with `sped on`; each run is a new browser publishing over WHIP and playing over WHEP, with the trial or without it, alternating, 20 runs each by default.
  - It prints one row per trial and job: the p50 and p95 in ms from `setRemoteDescription` to `connected`, and for the player to its first video frame, from the test page's events; and how many sessions SRS completed SPED for. A browser whose offer has no `sped` with the trial is a `SKIP`.

Helper scripts such as `gb28181-create-session.sh`, `gb28181-publish-stream.sh`, `proxy-e2e-origin.sh`, `setup-ffmpeg-with-whip.sh` and `windows-host-run.sh` are not standalone test cases unless a workflow explicitly invokes them. `windows-host-run.sh` syncs a checkout to the Windows test host and runs a script there, with `--wip` for the uncommitted working tree; its header lists the details.

A skill script with a `-test.sh` beside it, such as `srs-windows-runner-test.sh`, `windows-host-run-test.sh`, `obs-test-lib-test.sh`, `chrome-test-lib-test.sh` or `srs-full-tier-test.sh`, is tested by it on this machine with fakes; run it when the script changes.

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

Run every layer with `bash skills/srs-develop/scripts/srs-full-tier.sh`, after `full-tier-lock.sh status`. It runs them in the [Order](#order) below, under the [Global Lock](#global-lock), and prints one row per layer. With `--ssl-4` it builds and runs every layer on the bundled OpenSSL 4.1 build (`--ssl-4=on`); the OBS and Chrome Linux builds in Docker stay on the default build, and `srs-utest-run.sh --configure` switches `objs` back after it.

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
3. `obs-windows-test.sh`, only with `SRS_TEST_WINDOWS_HOST`: builds `srs.exe` on the Windows test host, then the same cases from OBS there to the native `srs.exe`.

Two variables control it:

- `SRS_TEST_OBS`: `1` by default; `0` skips the OBS builds and lane G, and the summary says `lane G: skipped (SRS_TEST_OBS=0)`.
- `SRS_TEST_WINDOWS_HOST`: the Windows test host, as a placeholder `<srs-test-windows-host>`; unset, the Windows build and run are left out.

**Chrome**, lane I:

1. `chrome-lane-test.sh`: the driver and helper unit tests, then the five Chrome cases against the local `objs/srs`.
2. `chrome-linux-test.sh`, after it: the same cases against SRS for Linux in Docker. Not in parallel: with both sets of browsers at once, players froze at their start on the Mac.
3. `chrome-windows-test.sh`, only with `SRS_TEST_WINDOWS_HOST`, beside the two above: builds `srs.exe` on the Windows test host, then the same cases from Edge there to the native `srs.exe`.

- `SRS_TEST_CHROME`: `1` by default; `0` skips the Chrome Linux build and lane I, and the summary says `lane I: skipped (SRS_TEST_CHROME=0)`.
- With no usable Node or browser, each case prints `SKIP <case>: unverified: Chrome not available (<why>)`, listed as unverified.

**Native Windows**, lane W: in Git Bash on Windows, where `uname -s` is `MINGW64*` or `MSYS*`, the Full tier runs lane W instead of the layers above, which need `./configure`, srs-bench, the proxy and Docker:

1. `srs-windows-runner.sh build`: the deps and `srs.exe`, with its start check.
2. `srs-windows-runner.sh --alone utest`, beside `obs-windows-test.sh --host-cases`, the OBS cases on that host, unless `SRS_TEST_OBS=0`.
3. `chrome-windows-test.sh --host-cases`, the Chrome cases there, alone, as lane I, unless `SRS_TEST_CHROME=0`. Beside the utest and the OBS cases, the host was too loaded and both the OBS and the Chrome cases failed.
4. `srs-windows-runner.sh --alone integ`, alone, as lane H.

There it needs only `go`, `ffmpeg` and `curl`, and does not read `SRS_TEST_WINDOWS_HOST`. From macOS or Linux, run it on the host with `windows-host-run.sh [--wip] <checkout> srs-windows-runner.sh full`. On macOS and Linux the Full tier never runs lane W, even with `SRS_TEST_WINDOWS_HOST` set.

Check each layer's exit code separately, and strip ANSI codes before counting `--- PASS`. The OBS and Chrome layers count their `PASS` and `FAIL` lines, and the `windows-*` rows the runner's `CHECK` and `START` lines and the gtest `PASSED` count. Each OBS or Chrome `SKIP` or `KNOWN` line is listed below the summary as unverified, never counted as passed.

`srs-full-tier-test.sh` tests the runner itself: the step order, lanes G and I, lane W with a fake `uname`, the `SRS_TEST_OBS`, `SRS_TEST_CHROME` and `SRS_TEST_WINDOWS_HOST` gates and the summary, with fake layers.

### Sub-agent

Run a Gate or Full tier in a sub-agent, so the build and test output stays out of the main agent's context. Give it the tier and the tree to test, and wait for its result instead of reading the logs.

The sub-agent runs the tier as in [Parallel Runs](#parallel-runs), keeps every log in a file, and returns only:

- The two lock lines.
- One row per layer or script: exit code, pass and fail counts, and duration.
- The unverified OBS and Chrome lines, or that lane G or I was skipped.
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

Beside these, unless `SRS_TEST_OBS=0`, `obs-linux-test.sh --build` builds SRS for Linux in Docker from the tracked files, in parallel, since it does not touch `trunk/objs`. Unless `SRS_TEST_CHROME=0`, `chrome-linux-test.sh --build` does the same in its own volume.

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
| I | The Chrome unit tests, then four Chrome cases in parallel and the SPED case after them (`chrome-lane-test.sh`), then the Linux run (`chrome-linux-test.sh`); skipped with `SRS_TEST_CHROME=0` |

**Step 3: lane H**, alone after Step 2: `make integ-unit`, then `make integ`. Its cases have time limits, and its timer cases measure SRS timers, which the load of the other lanes delays.

**Native Windows:** lane W in four steps, as in [Full Tier Layers](#full-tier-layers).

### Times

A normal Full tier on the 10-core arm64 Mac, with everything already built and every optional test on, measured on 2026-10-29:

| Step or lane | Seconds |
|---|---|
| Step 1, build | 1 |
| A, proxy | 31 |
| B, black-box fast | 26 |
| C, black-box slow | 32 |
| D, regression | 18 |
| E, feature scripts | 34 |
| F, utest | 35 |
| G, OBS | 84 |
| I, Chrome | 76 |
| Step 2, the slowest lane | 86 |
| H, integ | 33 |
| **Total** | **122** |

- Lane I was measured again on 2026-11-19, after the SPED case joined it: 74 and 76 s in two runs, `chrome-lane-test.sh` 43–44 s and `chrome-linux-test.sh` 29–32 s. The other rows are from 2026-10-29.
- Step 2 takes as long as its slowest lane, so a lane only matters when it becomes the slowest.
- A rebuild of `objs/srs` and the utest adds about 15 s, and the busy CPU slows every lane.
- The tests that use the Windows test host take turns on it, so their lanes wait for each other there.
- Lane W runs on the Windows test host by itself, in about 70 s.
- A script's own time limit is in its entry in [Feature-Specific Tests](#feature-specific-tests).
- When a change adds a test or makes a lane slower, run the Full tier twice and update this table.

## Port Plan

Each test process binds ports only from its own range, so the parallel lanes never collide.

- A new script or test takes its ports from its owner's range, or from the reserved range.
- Every range is a default that an environment variable moves without a code change, and none has to be set:
  - `SRS_TEST_PORT_BASE`, the first port of a script's slot or of the regression SRS.
  - `SRS_BLACKBOX_PORTS` and `SRS_UTEST_PORTS`, the range of an allocator, like `21000-25999`.
- No test binds a port below 11000, which is kept for human testing and shared services.
- No test binds a port from 32768, which covers the ephemeral ports of Linux (32768–60999), macOS and Windows (49152–65535): a client socket there makes a later bind fail.
- No test binds the Go proxy defaults 11935, 11985, 12025, 18000, 18080 and 20080 from `internal/env/env.go`, which are kept for running the proxy by hand.
- On the Windows test host, lane W binds the same ranges as on this machine: `srs-integ` 14000–14999, the C++ unit tests 26000–29999, the OBS cases of `obs-windows-test.sh` 15100–15399, the `build` step's start check 15500–15599, and the Chrome cases of `chrome-windows-test.sh` 15600–15999 and 16500–16599. The host lock keeps two runs apart there.
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
| 15500–15599 | `srs-windows-runner.sh` on the Windows test host, not the local machine: its `build` step starts `srs.exe` at `SRS_TEST_PORT_BASE` with RTMP +0, HTTP +1, API +2 | 100 | 3 | 97, inside the existing slot |
| 15600–15699 | `chrome-webrtc-test.sh` and `chrome-test-lib-test.sh --live` at `SRS_TEST_PORT_BASE`: RTMP +0, HTTP +1, API +2, RTC UDP +3, the driver's page server +9 | 100 | 5 | 95, inside the existing slot |
| 15700–15799 | `chrome-loss-test.sh` at `SRS_TEST_PORT_BASE`: four SRS, each with RTMP, HTTP, API and RTC UDP on +0..+3, +4..+7, +10..+13 and +14..+17; the driver's page server +9 | 100 | 17 | 83, inside the existing slot |
| 15800–15899 | `chrome-http-test.sh` at `SRS_TEST_PORT_BASE`: two SRS, each with RTMP, HTTP, API and RTC UDP on +0..+3 and +4..+7; the driver's page server +9 | 100 | 9 | 91, inside the existing slot |
| 15900–15999 | `chrome-codec-test.sh` at `SRS_TEST_PORT_BASE`: RTMP +0, HTTP +1, API +2, RTC UDP +3, the driver's page server +9 | 100 | 5 | 95, inside the existing slot |
| 16000–16399 | `chrome-linux-test.sh`: the first four Chrome cases at 16000, 16100, 16200 and 16300, with the ports of the 15600–15999 slots. Each SRS container publishes RTMP, HTTP and API on `127.0.0.1` and RTC UDP on the host's own IPv4; the driver's page server +9 is on the host | 400 | 36 | 364, inside the existing slots |
| 16400–16499 | Reserved for new test layers | 100 | 0 | 100 |
| 16500–16599 | `chrome-sped-test.sh` at `SRS_TEST_PORT_BASE`, lane I's base +900: two SRS, each with RTMP, HTTP, API and RTC UDP on +0..+3 and +4..+7; the driver's page server +9 | 100 | 9 | 91, inside the existing slot |
| 16600–16699 | `sped-measure.sh` at `SRS_TEST_PORT_BASE`: RTMP +0, API +1, RTC UDP +2 | 100 | 3 | 97, inside the existing slot |
| 16700–16799 | `chrome-sped-measure.sh` at `SRS_TEST_PORT_BASE`: one SRS with RTMP, HTTP, API and RTC UDP on +0..+3; the driver's page server +9 | 100 | 5 | 95, inside the existing slot |
| 16800–16899 | Reserved for new test layers | 100 | 0 | 100 |
| 16900–16999 | `chrome-linux-test.sh`: the SPED case at 16900, its base +900, with the ports of the 16500–16599 slot, published as for its other cases | 100 | 9 | 91, inside the existing slot |
| 17000–20999 | Reserved for new test layers, except the proxy defaults 18000, 18080 and 20080 | 4,000 | 0 | 3,997 |
| 21000–25999 | Black-box SRS servers, from `SRSPortAllocator` in `trunk/3rdparty/srs-bench/blackbox`, moved by `SRS_BLACKBOX_PORTS` | 5,000 | Random, per test | 0, owned by the allocator |
| 26000–29999 | C++ unit tests, from `srs_utest_random_port()`, `_srs_tmp_port` and `_srs_tmp_srt_port`, moved by `SRS_UTEST_PORTS` | 4,000 | Random, per test | 0, owned by the allocator |
| 30000–32767 | Reserved for new test layers | 2,768 | 0 | 2,768 |
| 32768–65535 | System: the kernel's ephemeral ports for client sockets and port-0 binds | 32,768 | 0 | 0, never bound by tests |
| **Total** | | **65,535** | | |
