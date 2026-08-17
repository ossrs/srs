# SRS Testing and Verification Map

Use this reference after `skills/internal-codemap-for-srs/SKILL.md` determines that a task requires tests, verification, reproduction, or benchmarking.

## C++ Media Server Verification

`trunk/src/utest/` — Unit tests for internal functions, classes, parsers, codecs, and configuration without starting SRS. Links directly against SRS source and uses mocks such as `MockSrsConfig`.

- Build and run: `cd trunk && ./configure --utest && make utest && ./objs/srs_utest`
- `srs_utest_ai01`–`ai24` — AI-written tests
- `srs_utest_manual_*` — Manually written tests
- `srs_utest_workflow_*` — Workflow and integration tests

`trunk/3rdparty/srs-bench/blackbox/` — Black-box tests against a self-managed SRS process. Each test starts SRS with `NewSRSServer()`, uses FFmpeg or FFprobe to publish and play, verifies output, and manages the server lifecycle.

- Build in `trunk/3rdparty/srs-bench/`; binary: `./objs/srs_blackbox_test`
- `rtmp_test.go` — RTMP
- `hls_test.go` — HLS
- `srt_test.go` — SRT
- `rtsp_test.go` — RTSP
- `hevc_test.go` — HEVC
- `dvr_test.go` — DVR
- `http_api_test.go` — HTTP API
- `mp3_test.go` — MP3

`trunk/3rdparty/srs-bench/srs/` — E2E protocol tests against an externally started SRS server. Uses real Pion WebRTC, RTMP, and HTTP API clients.

- Build in `trunk/3rdparty/srs-bench/`; binary: `./objs/srs_test`
- `rtc_test.go` — WebRTC
- `rtmp_test.go` — RTMP
- `srs_test.go` — General SRS workflows

`trunk/3rdparty/srs-bench/` — Performance and load benchmark, not a correctness test suite. The `./objs/srs_bench` binary simulates concurrent WHIP, WHEP, RTMP, reconnecting, DVR, plaintext RTC, and Janus workloads and reports metrics without pass/fail assertions.

## Verification Types

| Type | Server running? | Tests | Network | Pass/fail |
|---|---|---|---|---|
| Unit | No | Isolated internal logic | No | Yes |
| Black-box | Yes, self-managed | Whole-server behavior | Yes | Yes |
| E2E | Yes, externally managed | Protocol workflows | Yes | Yes |
| Benchmark | Yes, externally managed | Performance and capacity | Yes | No, metrics only |

Use the smallest relevant test during iteration, then run the complete required suite defined by the parent development workflow before declaring the task verified.
