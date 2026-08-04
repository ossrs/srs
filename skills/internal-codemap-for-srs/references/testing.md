# SRS Testing and Verification Map

Use this reference after `skills/internal-codemap-for-srs/SKILL.md` determines that a task requires tests, verification, reproduction, or benchmarking.

## Browser Client Verification

Browser player URL generation is verified independently with Node.js and does not require a running SRS server:

```bash
node skills/srs-develop/scripts/browser-page-url-test.js
```

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

## Next-Generation Go Proxy Verification

Run all commands from the repository root. Run the unit test first, then every E2E test in the listed order. E2E scripts bind fixed ports, so run them sequentially. Do not stop after an early success or failure; record every result, fix failures, and repeat until all required tests pass.

1. Go proxy unit tests with coverage:
   ```bash
   bash skills/srs-develop/scripts/proxy-utest.sh --coverage
   ```
2. Single-origin RTMP proxy:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-test.sh
   ```
3. Multi-origin memory load-balancer routing:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-cluster-test.sh
   ```
4. Proxy, SRS edge, and SRS origin three-tier topology with a late-joining player:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-edge-test.sh
   ```
5. Redis multi-proxy routing:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-redis-test.sh
   ```
6. RTMP publish with RTMP, HTTP-FLV, and HLS playback verification; WHEP remains a placeholder:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-transmux-test.sh
   ```
7. SRT publish with SRT, RTMP, HTTP-FLV, and HLS playback verification; WHEP remains a placeholder:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-srt-test.sh
   ```
8. WHIP publish with RTMP, HTTP-FLV, and HLS playback verification; WHEP remains a placeholder:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-whip-test.sh
   ```

The SRT test requires an FFmpeg build with libsrt. The WHIP test requires the `whip` muxer and OpenSSL. Both scripts automatically run `skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh` on macOS when no suitable FFmpeg is available.

## Verification Types

| Type | Server running? | Tests | Network | Pass/fail |
|---|---|---|---|---|
| Unit | No | Isolated internal logic | No | Yes |
| Black-box | Yes, self-managed | Whole-server behavior | Yes | Yes |
| E2E | Yes, externally managed | Protocol workflows | Yes | Yes |
| Benchmark | Yes, externally managed | Performance and capacity | Yes | No, metrics only |

Use the smallest relevant test during iteration, then run the complete required suite defined by the parent development workflow before declaring the task verified.
