# SRS Cross-Component Integration Tests

Use this suite for every standalone SRS runtime code change, whether the change is in the next-generation Go proxy or the C++ media server. Apply it during development, bug fixing, and pull-request review. The `proxy-*` filenames describe the test entry point, not a proxy-only scope: the scripts exercise the proxy, SRS origin and edge roles, Redis routing, and RTMP, HTTP-FLV, HLS, SRT, and WHIP interoperability.

Run focused and component-native tests first, then run every command below sequentially because the scripts bind fixed ports. Do not substitute unit tests, black-box tests, E2E protocol tests, or benchmarks for this suite, and do not skip a script merely because the edited file is outside the Go proxy. Continue after failures so every result is recorded; fix relevant failures and repeat the complete suite.

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

The SRT test requires an FFmpeg build with libsrt. The WHIP test requires the `whip` muxer and OpenSSL. Both scripts automatically run `skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh` on macOS when no suitable FFmpeg is available. If an environmental dependency is unavailable, run the script, preserve its exact result, and report the blocked coverage instead of claiming full verification.

Run feature-specific bundled tests in addition to this matrix when the routed workflow requires them, such as the browser URL test or GB28181 external-SIP cleanup tests. Helper scripts such as `gb28181-create-session.sh`, `gb28181-publish-stream.sh`, and `setup-ffmpeg-with-whip.sh` are not standalone test cases unless a workflow explicitly invokes them.
