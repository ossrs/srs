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
9. Bearer authentication startup validation, protected SRS, WHIP/WHEP, and proxy APIs, and authenticated origin registration:
   ```bash
   bash skills/srs-develop/scripts/proxy-e2e-bearer-auth-test.sh
   ```

The SRT test requires an FFmpeg build with libsrt. The WHIP test requires the `whip` muxer and OpenSSL. Both scripts automatically run `skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh` on macOS when no suitable FFmpeg is available. If an environmental dependency is unavailable, run the script, preserve its exact result, and report the blocked coverage instead of claiming full verification.

Run feature-specific bundled tests in addition to this matrix when the routed workflow requires them, such as the browser URL test or GB28181 external-SIP cleanup tests. Helper scripts such as `gb28181-create-session.sh`, `gb28181-publish-stream.sh`, and `setup-ffmpeg-with-whip.sh` are not standalone test cases unless a workflow explicitly invokes them.

## Verification Tiers

The trigger picks the tier, never the expected runtime. Report every layer not run as unverified.

1. **Iterate** — after each small change: the focused test plus all C++ and Go unit tests.
2. **Gate** — once per batch, before a commit that will be pushed or opened as a PR: Iterate, the suite above, and the black-box or regression cases for the touched protocol.
3. **Full** — when the user asks to run all tests, before merging a PR, and before pushing a backport or release. Run every layer, with no subset:
   - C++ unit: configure `trunk/` with the flags in `trunk/Dockerfile.test` (without `--build-cache`), remove stale `trunk/objs/Platform-*/utest/*.o`, then `make utest && ./objs/srs_utest`.
   - The suite above.
   - In `trunk/3rdparty/srs-bench` after `make test`: `./objs/srs_blackbox_test -test.v -test.run '^TestFast' -test.parallel 64`, then `-test.run '^TestSlow' -test.parallel 1`; pass `-srs-ffmpeg "$(command -v ffmpeg)" -srs-ffprobe "$(command -v ffprobe)"` because its lookup ignores `PATH`.
   - Regression: in `trunk/`, `./objs/srs -c conf/regression-test.conf`, wait 10s, run `./objs/srs_test -test.v` in `3rdparty/srs-bench`, then kill `$(cat objs/srs.pid)`.
   - `SRS_GB_SKIP_BUILD=1` with each `skills/srs-develop/scripts/gb28181-*-test.sh` (their build reconfigures `trunk/`), and `node skills/srs-develop/scripts/browser-page-url-test.js`.

   Check each layer's exit code separately, and strip ANSI codes before counting `--- PASS`.
