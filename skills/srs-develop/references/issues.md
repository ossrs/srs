# Issue Truth Records

Record only verified maintenance status and the latest maintainer-approved Truth Record. Never copy unverified issue discussion.

## #4697 [ENHANCEMENT] RTC audio pause/resume compatibility

- Issue: https://github.com/ossrs/srs/issues/4697
- Truth Record: https://github.com/ossrs/srs/issues/4697#issuecomment-5242316171
- Verified: 2026-08-10
- Branch: `develop`
- Commit: `8cba52441cc144d9b4f7e7963c7924e6e0849a10`
- Version: SRS `8.0.10`
- Environment: macOS 26.5.2, arm64
- Changes: None
- Closure: Declined compatibility enhancement; closed as not planned

**Background**

Normal WebRTC audio muting uses continuous encoded silence or negotiated Opus DTX while maintaining a correct RTP timeline. The reporter corrected the original DTX explanation: DTX was not negotiated, and the affected `flutter_webrtc` desktop stack uses a `webrtc-sdk` fork that sends no audio RTP packets while `track.enabled=false`, then resumes the lane later.

**Impact**

In this workflow, an audio pause of approximately N seconds can leave resumed audio approximately N seconds behind continuing video. This can produce one HLS segment approximately as long as the pause and temporarily increase `EXT-X-TARGETDURATION`.

**Why SRS does not support it**

Server-side compatibility logic would need to distinguish an intentional mute from DTX, packet loss, a network interruption, or publisher failure, then safely synthesize or rewrite timestamps. This would add substantial implementation and regression-test complexity across RTC synchronization, audio transcoding, audio-only and mixed streams, RTMP, and HLS.

Use a publisher that sends silent audio, correctly negotiates DTX, or preserves the RTP timestamp and RTCP sender-report timeline while its audio lane is stopped. SRS will not add complex timestamp heuristics for this client-specific behavior.

## #4690 [SECURITY] Unauthenticated proxy registration endpoint

- Issue: https://github.com/ossrs/srs/issues/4690
- Truth Record: https://github.com/ossrs/srs/issues/4690#issuecomment-5125571911
- Verified: 2026-07-29
- Branch: `forge`
- Commit: `65108f23edeeb9dd73ecbe5e804804ec50a3980e`
- Version: SRS `8.0.4`
- Environment: macOS 26.5.2, arm64; Go 1.25.0
- Changes: None
- Verification: Source, documentation, unit tests, and runtime reproduction

**Current state**

The proxy registration endpoint `/api/v1/srs/register` currently has no authentication by design. With a port-only configuration such as `PROXY_SYSTEM_API=12025`, it listens on all interfaces.

Testing confirmed that an unauthenticated caller who can reach this endpoint can register caller-controlled backend addresses. A subsequent HTTP-FLV request was routed to the injected backend and returned its content. Both memory and Redis load-balancer deployments share this registration path.

This does not immediately replace every existing stream mapping: existing mappings are sticky, while new streams select among registered backends. An attacker can still influence selection by registering multiple identities. In memory mode, the 300-second period is not a strict expiry because stale registrations and sticky mappings may remain usable.

**Current workaround**

Treat the System API as a trusted internal control-plane endpoint. Restrict it with private networking or firewall rules.

For authenticated access, bind it to loopback:

```bash
PROXY_SYSTEM_API=127.0.0.1:12025
```

Then expose an authenticated NGINX or application proxy that forwards approved registration requests to the loopback endpoint.

**Conclusion**

The reported exposure is real, but authentication is not part of the current API design. Built-in registration authentication is a valuable future security feature.

SRS is currently prioritizing the project foundation required for reliable AI maintenance, so authentication will not be implemented in the short term. No immediate project change is planned; keep the issue open for future support.

## #4686 [FEATURE] Media over QUIC support

- Issue: https://github.com/ossrs/srs/issues/4686
- Truth Record: https://github.com/ossrs/srs/issues/4686#issuecomment-5004294564
- Verified: 2026-07-17
- Checked: 2026-07-18; no later updates
- Branch: `develop`
- Commit: `e03c841dc442c6d5059cddc0e2f2e6cc4a89d087`
- Version: SRS `8.0.3`
- Environment: macOS 26.5.2, arm64; source and documentation review only
- Changes and tests: None

**Current state**

SRS does not support Media over QUIC. This is a valid deferred feature request, not a bug. Major new protocols will not be added to the current C++ server. The priority is the Go proxy, AI maintenance workflow, Go origin parity, then Go edge and protocols such as MoQ.

MoQ is still evolving; the verified specification was `draft-ietf-moq-transport-19`. A future implementation must define interoperability, roles, formats, codecs, authentication, and SRS integration.

**Conclusion**

No project change is required. Retain the issue and revisit it after the next-generation origin and edge foundations are ready.

**Unknowns**

Schedule, MOQT versus moq-lite, origin/edge/relay roles, media formats, codecs, and browser interoperability.

## #4684 [BUG] Blocking DNS resolution stalls State Threads

- Issue: https://github.com/ossrs/srs/issues/4684
- Truth Record: https://github.com/ossrs/srs/issues/4684#issuecomment-5011850798
- Verified: 2026-07-18
- Checked: 2026-07-18; no later updates
- Branch: `develop`
- Commit: `e03c841dc442c6d5059cddc0e2f2e6cc4a89d087`
- Version: SRS `8.0.3`
- Environment: macOS 26.5.2, arm64
- Verification method: Source-code, project-history, and prior reproduction review
- New runtime reproduction: Not performed
- Changes and tests: None

**Problem**

The current equivalent of the issue's `srs_socket_connect()` is [`srs_tcp_connect()`](https://github.com/ossrs/srs/blob/e03c841dc442c6d5059cddc0e2f2e6cc4a89d087/trunk/src/protocol/srs_protocol_st.cpp#L208-L251). It calls the synchronous system `getaddrinfo()` function directly before creating the socket and calling the coroutine-aware `st_connect()`.

The supplied connection timeout applies to `st_connect()` only. It does not limit how long `getaddrinfo()` may block.

**Confirmed facts**

- The C++ SRS media server runs its main network workload as cooperative State Threads coroutines on one business thread.
- A blocking `getaddrinfo()` call does not yield to the State Threads scheduler.
- While `getaddrinfo()` is blocked, the other coroutines scheduled on the same business thread cannot progress.
- Consequently, ongoing publishing, playback, forwarding, WebRTC, and other stream processing on that thread can pause during a slow DNS lookup.
- Forwarding to a domain name can trigger this path: `SrsForwarder::do_cycle()` → RTMP client → `SrsTcpClient::connect()` → `srs_tcp_connect()` → `getaddrinfo()`.
- An inbound publisher does not normally perform DNS resolution itself, but its stream processing can still pause when another coroutine blocks the shared business thread.
- The configured FFmpeg ingest feature launches a separate FFmpeg process. DNS resolution performed inside that external process does not directly block the SRS business thread.
- This is a previously known limitation. Issue [#2112](https://github.com/ossrs/srs/issues/2112) discussed replacing the blocking system DNS resolution.
- Issue [#3463](https://github.com/ossrs/srs/issues/3463) recorded a runtime case in which DNS resolution blocked SRS processing. Its maintainer response explicitly identified #2112 as the general solution.
- The general non-blocking DNS solution from #2112 is not present in the current `develop` source. The mitigation applied for #3463 disabled automatic version querying by default, but it did not make SRS DNS resolution non-blocking.

**Conclusion**

The report is confirmed. Synchronous DNS resolution in `srs_tcp_connect()` can block the main State Threads business thread. Forwarding to a domain name can trigger the lookup, and ongoing stream processing on the same thread can be affected.

External FFmpeg ingest is a separate process. However, ordinary inbound publishing handled by SRS can still pause while the SRS business thread is blocked by DNS resolution elsewhere.

**Current workaround**

SRS does not currently provide an internal workaround for blocking DNS resolution. Avoid configuring domain names for SRS-managed outbound connections. Use numeric IP addresses or localhost instead.

For HTTP hooks or callbacks that must ultimately reach a domain name, run a local sidecar or worker HTTP service, for example in Go. Configure SRS to send the callback to `127.0.0.1`; the sidecar performs DNS resolution and forwards the request to the remote domain. This keeps DNS resolution outside the SRS business thread.

## #4681 [FEATURE] Enhanced RTMP v2 multitrack audio

- Issue: https://github.com/ossrs/srs/issues/4681
- Truth Record: https://github.com/ossrs/srs/issues/4681#issuecomment-5084234253
- Verified: 2026-07-26
- Branch: `forge`
- Commit: `21f94c953735675f159405ba00f426c3f7e98305`
- Version: SRS `8.0.4`
- Environment: macOS 26.5.2, arm64; FFmpeg 8.1.1
- Supersedes: None
- Changes and tests: No project change. Reproduced with one H.264 video track and two AAC audio tracks.

**Current state**

SRS does not currently support Enhanced RTMP v2 multitrack audio. Publishing a stream containing Enhanced RTMP v2 audio headers causes SRS to reject `SoundFormat=9` as unsupported.

The limitation exists in both the reported SRS 7.0.147 source and the current SRS 8.0.4 source. The current media model maintains one audio codec, parser, and sequence header per live stream and has no per-track state.

The targeted reproduction generated a two-second FLV stream containing one H.264 video track and two independent AAC audio tracks. `ffprobe` confirmed all three tracks. Publishing it to the locally built SRS 8.0.4 terminated the publisher with `Broken pipe`; SRS reported:

```text
rtmp: consume audio : format consume audio : unsupported audio codec=9(Other)
```

FFmpeg 8 supports multitrack audio and video using Enhanced FLV v2. In the Enhanced RTMP v2 specification, `SoundFormat=9` identifies the extended audio header used for features including multitrack audio:

- https://github.com/FFmpeg/FFmpeg/blob/master/Changelog
- https://github.com/veovera/enhanced-rtmp/blob/main/docs/enhanced/enhanced-rtmp-v2.md

**Project direction**

Enhanced RTMP v2 multitrack support is a valuable feature that SRS intends to support in the future. It belongs to the same class of substantial protocol work as Media over QUIC and other next-generation media protocols.

This feature is not the current priority. SRS is presently refining its code structure, documentation, knowledge base, development methods, and verification workflows so that AI can reliably manage and maintain the project.

After this AI-maintenance foundation is mature, the project can revisit Enhanced RTMP v2 and other major protocol features.

**Conclusion**

This is a valid future feature request, not a regression bug. Keep the issue open for future consideration. No immediate project change is required.

**Current workarounds**

Publish only one selected audio track, mix the tracks into one audio stream before publishing, or publish independent tracks under separate stream URLs.

**Unknowns**

- Implementation schedule
- Whether support belongs in the C++ server or the next-generation Go server
- Required capability negotiation and interoperability
- Track-selection behavior for RTMP playback, forwarding, HLS, WebRTC, recording, and other outputs
- Whether the first implementation should support audio only or complete Enhanced RTMP v2 audio and video multitrack functionality

## #4671 [BUG] Incomplete WebRTC RTX handling

- Issue: https://github.com/ossrs/srs/issues/4671
- Truth Record: https://github.com/ossrs/srs/issues/4671#issuecomment-5151313612
- Verified: 2026-08-01
- Branch: `forge`
- Commit: `65108f23edeeb9dd73ecbe5e804804ec50a3980e`
- Version: SRS `8.0.4`; issue reported against SRS `7.0.145`
- Environment: macOS 26.5.2, arm64; source, history, documentation, and PR review only
- Related PR: https://github.com/ossrs/srs/pull/4644
- Changes and tests: None; no runtime reproduction
- Supersedes: None

**Current state**

SRS supports NACK when the publisher resends the original RTP packet with the same SSRC, payload type, and sequence number.

SRS does not fully support RFC 4588 retransmission using a separate RTX SSRC:

1. FID association fails because negotiation searches inactive tracks, leaving `rtx_ssrc_ = 0`.
2. RTX packets are not unwrapped to the original SSRC, payload type, and sequence number.
3. The WHIP SDP answer does not advertise RTX.

Therefore, RTX RTP and Sender Reports can be rejected as unknown SSRCs.

**Related PR**

PR #4644 fixes FID association and RTX packet unwrapping, but it is still open and conflicting. It does not yet cover SDP answer negotiation, RTX RTCP handling, or end-to-end verification.

**Conclusion**

This is a valid missing feature with a confirmed initialization bug, not a regression in same-SSRC NACK retransmission. Handle this issue together with PR #4644; do not use the proposed early `is_active_ = true` workaround as the final fix.

## #4663 [USAGE] WHEP stream name incorrectly includes `.flv`

- Issue: https://github.com/ossrs/srs/issues/4663
- Truth Record: https://github.com/ossrs/srs/issues/4663#issuecomment-5151376223
- Verified: 2026-08-01; closed as a usage error with no project change

**Conclusion**

The publisher used stream `livestream`, but WHEP requested `livestream.flv`. WHEP treats `.flv` as part of the stream name; it is only appropriate for HTTP-FLV URLs. Use `stream=livestream`. This is user URL misuse, not an SRS bug, and the existing documentation is sufficient.

## #4656 [BUG] Live-source cleanup can separate publishers and players

- Issue: https://github.com/ossrs/srs/issues/4656
- Truth Record: https://github.com/ossrs/srs/issues/4656#issuecomment-5161223806
- Verified: 2026-08-02
- Branch: `forge`
- Commit: `4843c69d8df2aea45bde8f73e5ed2fa72e3a924f`, with an uncommitted candidate fix
- Version: SRS `8.0.4`; another user reported the problem on SRS `6.0.166`
- Environment: macOS 26.5.2, arm64; FFmpeg/FFprobe 8.1.1
- Supersedes: None; no previous authorized Truth Record
- Changes: Candidate fix and regression tests, not committed or released

### Reported problem

An affected stream can continue receiving publisher data while new RTMP and HTTP-FLV players receive no media. SRS logs show players creating a consumer with `active=0`. HLS can continue playing the same stream.

Restarting SRS, or kicking the publisher and allowing it to republish, restores playback. A [second report](https://github.com/ossrs/srs/issues/4656#issuecomment-4610706424) described the same intermittent behavior on SRS 6.0.166.

### Confirmed root cause

The publisher acquires its global stream publish token before fetching the live source.

A race is possible after the publisher fetches source **A** but before `acquire_publish()` activates it:

1. The publisher acquires the stream publish token.
2. It fetches source **A** from the live-source manager.
3. It yields before calling `acquire_publish()`.
4. Because source **A** is not active yet, the cleanup timer can consider it dead and erase it from the manager's source pool.
5. The publisher still retains a shared pointer to source **A**, so it later activates and publishes media into **A**.
6. A new player cannot find **A** in the pool and creates source **B** for the same stream URL.
7. Source **B** has no publisher, so the player gets `active=0` and no media.

HLS can continue because the publisher and its existing HLS pipeline still reference source **A**, while new RTMP and HTTP-FLV players use source **B**.

### Deterministic reproduction

A test-only environment variable was added before `acquire_publish()`:

```bash
SRS_TEST_PUBLISH_BEFORE_ACQUIRE_DELAY=30000
```

With a cleanup delay shorter than 30 seconds, this forces the pending publisher to yield long enough for source cleanup.

Without the cleanup fix:

- SRS removes source **A** during the injected delay.
- The publisher subsequently activates its retained source **A**.
- New RTMP and HTTP-FLV consumers report `active=0` and receive no stream.
- HLS continues from source **A**.

This reproduces the defining symptoms of #4656.

### Candidate fix

The existing stream publish token now acts as a liveness guard for the pending source.

`SrsStreamPublishTokenManager` exposes whether a stream URL's token is acquired. Live-source cleanup removes a dead source only when its publish token is not acquired:

```cpp
bool is_stream_acquired = _srs_stream_publish_tokens &&
    _srs_stream_publish_tokens->is_acquired(it->first);

if (source->stream_is_dead() && !is_stream_acquired) {
    // Remove the dead source.
}
```

This preserves the source-pool entry while a publisher owns the stream token, including while it yields before activation. Normal cleanup resumes after the token is released.

### Verification

With the candidate fix and the 30-second injected delay:

- The source was not removed during the delay.
- The publisher activated the same source retained in the pool.
- HTTP-FLV playback succeeded with H.264 video and AAC audio.
- RTMP playback succeeded with H.264 video and AAC audio.
- Players created consumers with `active=1`.

The deterministic unit regression test directly creates the dangerous state: a dead source in the pool with an acquired publish token.

Negative control:

```text
Without cleanup guard:
Expected pool size: 1
Actual pool size:   0
FAILED: ReproduceIssue4656.PublishTokenKeepsPendingLiveSource
```

With the cleanup guard restored:

```text
PASSED: ReproduceIssue4656.PublishTokenKeepsPendingLiveSource
```

The complete unit-test suite passed: 2,188 tests from 274 suites. A normal non-ASan SRS production build also succeeded.

### Conclusion

This is a confirmed source-lifecycle race. A publisher can activate a source that the live-source manager has already removed, causing the publisher and new players to reference different source objects for the same stream URL.

Keeping the source in the manager while its publish token is acquired is the smallest direct fix. The deterministic reproduction, negative-control test, fixed regression test, protocol playback checks, complete unit-test suite, and production build all support the fix.

The candidate change remains uncommitted and unreleased pending maintainer review.

### Unknowns

The original deployments did not provide enough scheduling-level evidence to prove that every reported occurrence followed this exact race. However, the deterministic reproduction matches the reported publisher-active, player-`active=0`, and HLS-still-playing behavior.

## #4647 [FEATURE] Configurable proxy origin registration TTL

- **Issue:** https://github.com/ossrs/srs/issues/4647
- **Truth Record:** https://github.com/ossrs/srs/issues/4647#issuecomment-5173896319
- **Verified:** 2026-08-03
- **Branch:** `develop`
- **Commit:** `b6b70164cd3bc665040d387105a77420ced22fd3`
- **Version:** SRS `8.0.6`
- **Environment:** macOS 26.5.2, arm64; Go 1.25.0
- **Merged PR:** https://github.com/ossrs/srs/pull/4694
- **Earlier PR:** https://github.com/ossrs/srs/pull/4650 — closed without merging
- **Supersedes:** None; no previous authorized Truth Record

### Reported problem

The proxy used a fixed 300-second lifetime for origin registrations. Some deployments have shorter heartbeat and failover requirements and need to configure this lifetime.

### Current state

SRS Proxy now supports:

```bash
PROXY_ORIGIN_SERVER_TTL=45s
```

The value accepts positive Go duration syntax such as `45s` or `2m`. Invalid, zero, and negative values cause load-balancer initialization to fail. When unset, the default remains 300 seconds.

One parser, `parseOriginServerTTL`, owns the default and validation. Both memory and Redis load balancers obtain the configured value during initialization.

### Load-balancer behavior

- **Memory load balancer:** Origins with heartbeats inside the configured lifetime are preferred. As before, if no origins are healthy, it falls back to all registered origins.
- **Redis load balancer:** The configured lifetime is applied directly to each origin-registration Redis key. An expired registration is unavailable for new routing.
- Stream mappings remain persistent and are not controlled by this setting.

### Scope decision

Only the origin-registration lifetime is configurable.

The fixed 120-second HLS and WebRTC session-cache lifetimes remain unchanged. They represent internal transient session state, are unrelated to origin heartbeat/failover requirements, and currently have no demonstrated need for operator configuration.

The earlier PR #4650 proposed configuring all three lifetimes. The merged implementation intentionally uses only `PROXY_ORIGIN_SERVER_TTL`.

### Verification

- Proxy unit tests with coverage passed: **65.5%**
- Single-origin RTMP E2E passed
- Multi-origin memory load-balancer E2E passed
- Proxy-edge-origin E2E passed
- Redis multi-proxy E2E passed
- RTMP transmux E2E passed
- SRT proxy E2E passed
- WHIP proxy E2E passed
- Final targeted `internal/lb` unit tests passed
- `git diff --check` passed

Unit coverage verifies the default, custom duration, invalid value, zero, and negative cases. It also verifies configured memory-LB health selection and the Redis registration-key TTL.

### Conclusion

Issue #4647 is resolved in SRS 8.0.6. Operators can configure the origin-registration lifetime consistently for memory and Redis load balancers while the default behavior remains backward compatible.

### Unknowns

No dedicated E2E test waits for a short configured lifetime to expire in real time. The TTL parsing and both load-balancer applications are covered by unit tests, while the complete default-configuration proxy workflows are covered by E2E tests.

## #4646 [FEATURE] Proxy origin and stream-mapping query APIs

- **Issue:** https://github.com/ossrs/srs/issues/4646
- **Truth Record:** https://github.com/ossrs/srs/issues/4646#issuecomment-5180281566
- **Verified:** 2026-08-04
- **Branch:** `forge`
- **Commit:** `ae221b5e2c13bbfb9f51ffe70ed57ae32da423cf`
- **Version:** SRS `8.0.6`
- **Environment:** macOS 26.5.2, arm64; Go 1.25.0
- **Changes/tests:** None

SRS Proxy supports registering origins but does not provide APIs for querying registered origins or stream-to-origin mappings.

An origin query API would be valuable for debugging and verifying registrations. A stream mapping API would also be useful. However, these APIs—and other operational APIs—need careful, comprehensive design.

This is a valuable feature request, but it is deferred while higher-priority bugs are addressed. Keep the issue open and revisit it when there is time to design the API properly.

## #4645 [BUG] Browser player URLs fail behind reverse proxies

- **Issue:** https://github.com/ossrs/srs/issues/4645
- **Truth Record:** https://github.com/ossrs/srs/issues/4645#issuecomment-5180983219
- **Verified:** 2026-08-04
- **Branch:** `forge`
- **Commit:** `df73ac14de5e26bec66fa4ded4b9a159dec4b9f1`, with a staged candidate fix
- **Version:** SRS `8.0.6`
- **Environment:** macOS 26.5.2, arm64; Node.js 22.22.0
- **Supersedes:** None; no previous authorized Truth Record
- **Release state:** Uncommitted and unreleased

### Reported problem

The browser HTTP-FLV/HLS player generated incorrect media URLs when accessed through HTTP or HTTPS reverse proxies.

Without explicit query overrides, `build_default_flv_url()` always selected HTTP port 8080. HTTPS selected port 1935. This could bypass the public reverse proxy, produce mixed-content requests, or target a closed port.

### Confirmed root cause

The browser player constructed its default media endpoint from inconsistent inputs:

- Hostname came from `window.location.hostname`.
- Scheme defaulted to `http`.
- Port defaulted to 8080 for HTTP and 1935 otherwise.
- The public page protocol and port were ignored.

`is_default_port()` itself was correct. Explicit HTTP port 80 and HTTPS port 443 were already omitted properly.

`parse_query_string()` was also working as designed: optional `schema`, `server`, `port`, `vhost`, `app`, and `stream` properties appear only when supplied in the query string.

### Candidate fix

`trunk/research/players/js/srs.page.js` now:

1. Uses the player page's protocol and public port by default.
2. Omits standard HTTP port 80 and HTTPS port 443.
3. Preserves direct SRS access on port 8080.
4. Preserves explicit `schema`, `server`, `port`, and `vhost` overrides.
5. Uses the standard port for an explicitly selected protocol when it differs from the page protocol.

### Regression coverage

Added:

```text
skills/srs-develop/scripts/browser-page-url-test.js
```

Run with:

```bash
node skills/srs-develop/scripts/browser-page-url-test.js
```

The testing command is recorded in:

```text
skills/internal-codemap-for-srs/references/testing.md
```

All six cases passed:

- Direct SRS HTTP server on port 8080
- Public HTTP origin on port 80
- Public HTTPS origin on port 443
- Custom reverse-proxy port
- Explicit protocol change without a port
- Explicit target overrides

JavaScript syntax checks and `git diff --check` also passed.

### Conclusion

The default browser URL-generation bug is confirmed, and the staged candidate fixes it for player URLs without explicit target overrides.

The fix is uncommitted and unreleased pending maintainer review.

### Remaining unknowns

- A real browser/reverse-proxy playback test has not been performed.
- The issue's example page URL explicitly contains `port=8080`. Explicit overrides remain authoritative. If another page or console automatically inserts that parameter, its URL generator requires a separate fix.
- The reporter did not provide the exact SRS version, browser, or complete reverse-proxy configuration.

## #4642 [BUG] Duplicate WebRTC TCP owner can leave a stale session pointer

- **Issue:** https://github.com/ossrs/srs/issues/4642
- **Truth Record:** https://github.com/ossrs/srs/issues/4642#issuecomment-5207071995
- **Title:** `Bug: SIGSEGV in SrsRtcTcpConn when WebRTC-over-TCP viewer disconnects (v6.0.184)`
- **Verified:** 2026-08-06
- **Issue state:** Open, no comments before this Truth Record, label `TransByAI`
- **Reported version:** SRS `6.0.184` / `63edbef90864d425a6303c64bae9600631a4c0f9`
- **Current checked source:** SRS `8.0.7`, remote `develop` `ee0c5cd98a38966a982d675f6533e85acdbca6dd`
- **Unfixed baseline:** `d5ab7db9a9ff7471fedb4ff857fba0645c4dc85e`
- **Candidate fix:** `5f4d5c9a40501d8e0e46ddbe304806a34c51a637` on branch `forge`
- **Verification:** macOS 26.5.2 arm64, ASAN-enabled C++ utests
- **Runtime reproduction:** Not done; no browser/Docker production SIGSEGV was reproduced
- **Release state:** Fix is local only; not merged or released

### Summary

The issue's exact explanation is wrong: since PR #4083 in SRS `6.0.127`, the recorded WebRTC-over-TCP owner is interrupted during RTC network destruction, and `SrsRtcTcpConn::interrupt()` clears that owner's `session_` pointer.

But the code still had a real ownership bug that can explain the reported crash location.

### Confirmed bug

Before the fix, a second TCP connection for the same RTC session could replace the first connection as owner before the code checked uniqueness:

```cpp
if (network->owner().get() != this) {
    network->set_owner(*wrapper_);
    session_ = session;
}
if (network->owner().get() != this) {
    return srs_error_new(ERROR_RTC_TCP_UNIQUE, "only support one network");
}
```

Bad sequence:

1. Connection A handshakes and stores the RTC session pointer.
2. Connection B handshakes for the same RTC session.
3. B replaces A as network owner.
4. A still has a raw `session_` pointer, but teardown will only interrupt B.
5. A can later dereference stale memory at `session_->tcp()`, matching the reported crash area.

The dummy owner was not the direct cause. The direct cause was **replace before reject**. The dummy owner made that pattern easier to write because the first real connection had to replace something.

### Regression test

Added utest:

```text
ReproduceIssue4642.RejectSecondTcpConnForSameRtcSession
```

It creates one mock RTC session and two TCP connections with the same ICE username.

Expected behavior:

- A succeeds and becomes owner.
- B fails with `ERROR_RTC_TCP_UNIQUE`.
- A remains owner.
- B never stores the session pointer.

On the unfixed baseline, the test failed because B succeeded, replaced A, and stored the session pointer. This reproduces the stale-pointer prerequisite deterministically without intentionally crashing the test process.

### Fix

The fix makes the TCP owner empty initially, then rejects any second owner before changing state:

```cpp
SrsRtcTcpNetwork *network = dynamic_cast<SrsRtcTcpNetwork *>(session->tcp());
if (network->owner().get()) {
    return srs_error_new(ERROR_RTC_TCP_UNIQUE, "only support one network");
}
network->set_owner(*wrapper_);
session_ = session;
```

The network destructor now checks for an owner before interrupting it:

```cpp
if (owner_.get()) {
    owner_->interrupt();
}
```

### Verification after fix

Build:

```bash
cd trunk
./configure --utest --gb28181=on --sanitizer=on --build-cache=off
make utest
```

Targeted tests:

```bash
ASAN_OPTIONS=detect_leaks=0 ./objs/srs_utest \
  --gtest_filter='RtcTcpConnTest.*:ReproduceIssue4642.*'
```

Result: 5 tests passed.

Full C++ utest suite:

```bash
ASAN_OPTIONS=detect_leaks=0 ./objs/srs_utest
```

Result: 2239 tests passed.

### Conclusion

This issue is **partially confirmed**.

- Not confirmed: the issue's exact claim that `session_` is never cleared for the recorded owner.
- Confirmed: a second TCP connection could displace the first owner and leave the first connection with a stale raw session pointer.
- Fixed locally: duplicate TCP owners are rejected before ownership/session state changes.
- Still needed before closing: review, merge, release, and ideally reporter/runtime confirmation that the original SIGSEGV used this two-connection path.

### Unknowns

- Whether the reporter's deployment actually created two TCP connections for one RTC session.
- Whether every reported crash followed this path.
- The reporter's exact Docker image digest.
- Whether a production ASAN reproducer would show the same sequence.

## #4641 [USAGE] IPv6 RTMP listener requires explicit configuration

- **Issue:** https://github.com/ossrs/srs/issues/4641
- **Truth Record:** https://github.com/ossrs/srs/issues/4641#issuecomment-5203763730
- **Verified:** 2026-08-06
- **Branch:** `forge`
- **Commit:** `ee0c5cd98a38966a982d675f6533e85acdbca6dd`
- **Version:** SRS `8.0.7`
- **Changes:** None

SRS supports IPv6 RTMP listening since v7.0.67. Configure it explicitly:

```conf
rtmp {
    listen [::]:1935;
}
```

The report provides no SRS version, configuration, logs, or network details, so an SRS bug cannot be established. This is most likely a deployment or listener-configuration issue.

No project change is required. The reporter should use the IPv6 listener configuration and provide complete logs and configuration if the problem remains.

## #4634 [USAGE] Classic edge does not support WebRTC

- **Issue:** https://github.com/ossrs/srs/issues/4634
- **Truth Record:** https://github.com/ossrs/srs/issues/4634#issuecomment-5216829576
- **Verified:** 2026-08-07
- **Branch:** `forge`
- **Commit:** `2bb5e4428f393d09546f53f46ffbdd246dedcd95`
- **Version:** SRS `8.0.9`
- **Changes:** None
- **Closure:** Expected behavior; issue closed.

Edge RTC disable is expected and already documented: classic Edge supports RTMP/HTTP-FLV, not WebRTC. Users should check Edge docs or ask SRS AI before opening usage questions already covered by docs.

## #4633 [USAGE] Configure HTTP-FLV header track flags

- **Issue:** https://github.com/ossrs/srs/issues/4633
- **Truth Record:** https://github.com/ossrs/srs/issues/4633#issuecomment-5216986292
- **Verified:** 2026-08-07
- **Branch:** `forge`
- **Commit:** `2bb5e4428f393d09546f53f46ffbdd246dedcd95`
- **Version:** SRS `8.0.9`
- **Changes:** None
- **Closure:** Not closed yet; usage/configuration issue, no project change.

The reported `0x01` is the HTTP-FLV header flag for video-only, not an RTMP header. SRS already supports the requested behavior: for RTC-to-HTTP-FLV playback that must advertise both tracks from the first FLV header, configure `http_remux { has_audio on; has_video on; guess_has_av off; }`; users should read the HTTP-FLV docs or ask SRS AI before opening documented-configuration issues.

## #4632 [USAGE] Origin RTMP handshake timeout keeps the edge publisher busy

- **Issue:** https://github.com/ossrs/srs/issues/4632
- **Truth Record:** https://github.com/ossrs/srs/issues/4632#issuecomment-5217342826
- **Verified:** 2026-08-07
- **Branch:** `forge`
- **Commit:** `ce50bbe975912458ffb85ff82f8c6795c221c8bb`
- **Version:** SRS `8.0.9`
- **Changes:** None
- **Closure:** Upstream RTMP handshake timeout; issue closed.

The edge established TCP, but the origin did not complete the RTMP handshake within 30 seconds. During that wait, the current publisher owns the edge stream, so another publisher for the same stream is correctly rejected as busy; the state resets after timeout. No SRS defect was confirmed.

## #4631 [BUG] Forward backend failure leaves the live source busy

- **Issue:** https://github.com/ossrs/srs/issues/4631
- **Truth Record:** https://github.com/ossrs/srs/issues/4631#issuecomment-5218171343
- **Verified:** 2026-08-07
- **Branch:** `forge`
- **Commit:** `70309a6a21a2b9a148b11a03cd40ab806087e7cc`
- **Version:** SRS `8.0.9`
- **Changes:** Fixed on `forge`; not merged or released
- **Verification:** Runtime reproduction and 2,190 passing unit tests

### Current state

A dynamic-forward backend error such as HTTP 500 correctly rejects the current publication because SRS cannot obtain valid forwarding destinations. Before the fix, that error left the live source permanently busy: later publishers received `StreamBusy`, the stream was absent from `/api/v1/streams/`, and only restarting SRS cleared it.

`SrsLiveSource::on_publish()` marks the source as publishing before initializing the origin hub and querying the backend. When backend discovery failed, `acquire_publish()` returned an error, but the publishing workflow released the source only after successful acquisition. The global publish token was released; the live source's local publish state was not.

### Resolution and verification

The publishing workflow now releases publish state after successful or failed acquisition, except for `StreamBusy`. That exception is required because a busy stream belongs to another publisher and must not be released by the rejected session. The original backend error is still returned, and the external unpublish hook remains limited to sessions that successfully entered the publish lifecycle.

The regression test performs two consecutive backend failures and verifies that both reach the backend, both return the original HTTP error, and the source is available after each attempt. Runtime verification confirmed that a publication rejected by HTTP 500 can be retried successfully after the backend recovers, without restarting SRS. All 2,190 unit tests passed.

### Workaround

For released versions, return HTTP 200 with `code: 0` and empty `urls` to accept publishing without forwarding. Restart SRS to clear an already stuck stream.

## #4639 [BUG] Missing CRLF after SDP SSRC group

- **Issue:** https://github.com/ossrs/srs/issues/4639
- **Truth Record:** Pending maintainer publication
- **Verified:** 2026-08-07
- **Branch:** `forge`
- **Commit:** `9be7486e751a67bb809cc9926e4a19da262e25b7`, with a staged candidate fix
- **Version:** SRS `8.0.8`; issue reported against SRS `6.0-r0`
- **Environment:** macOS arm64, Apple Clang, ASAN utest build
- **Supersedes:** `NO_TRUTH` 2026-02-28 — report identified concatenated SDP lines when downlink SSRC groups are present, but had no maintainer verification.

### Confirmed facts

RFC 8866 defines CRLF as the SDP line terminator. Parsers should tolerate LF-only lines, but this bug is neither CRLF nor LF: `SrsSSRCGroup::encode()` emitted no line terminator.

The defect is local to `SrsSSRCGroup::encode()` in `trunk/src/protocol/srs_protocol_sdp.cpp`. Before the fix, encoding an SSRC group followed by another SDP attribute produced concatenated output:

```text
a=ssrc-group:FID 12345 67890a=ssrc:12345 cname:test-cname\r\n
```

Other SDP encoders already append `kCRLF`; this is not a general SDP encoding problem.

### Fix and regression coverage

The candidate fix adds `os << kCRLF;` after the SSRC group SSRC list.

Regression coverage:

- `ProtocolSdpTest.SrsSSRCGroupEncode` now requires the exact output `a=ssrc-group:FID 12345 67890\r\n`.
- `ProtocolSdpTest.SrsSSRCGroupEncodeBeforeSsrcInfo` verifies that a following `a=ssrc:` line is separated, not concatenated.

Before the production fix, both regression tests failed. After the fix:

- `make utest -j4` passed.
- `ASAN_OPTIONS=detect_leaks=0 ./objs/srs_utest --gtest_filter='ProtocolSdpTest.SrsSSRCGroupEncode*'` passed: 2 tests passed.
- `ASAN_OPTIONS=detect_leaks=0 ./objs/srs_utest` passed: 2240 tests passed.

### Conclusion

#4639 is a real narrow SDP formatting bug in `SrsSSRCGroup::encode()`. The root cause is a missing line terminator after the encoded `a=ssrc-group:` attribute. The local code change fixes the concatenation by appending `kCRLF`.

### Unknowns

Current `SrsMediaDesc::encode()` does not appear to serialize `ssrc_groups_` in the normal media-description path, so the exact runtime path from the report depends on the downlink SSRC-group usage path.

## #4629 [USAGE] GB28181 uses one shared media listener

- **Issue:** https://github.com/ossrs/srs/issues/4629
- **Truth Record:** https://github.com/ossrs/srs/issues/4629#issuecomment-5223834511
- **Verified:** 2026-08-07
- **Branch:** `forge`
- **Commit:** `ce50bbe975912458ffb85ff82f8c6795c221c8bb`
- **Version:** SRS `8.0.9`
- **Changes:** None
- **Closure:** Expected behavior; issue closed.

SRS supports one GB28181 media listener by design; all cameras should use that configured port. The old `sip.listen` was a separate embedded SIP service, not another media listener, and current versions require an external SIP server. Users should read the GB28181 documentation or ask SRS AI before opening usage questions about documented behavior.

## #4626 [UNCONFIRMED] WHEP playback has no media on an old SRS version

- **Issue:** https://github.com/ossrs/srs/issues/4626
- **Truth Record:** https://github.com/ossrs/srs/issues/4626#issuecomment-5229451675
- **Verified:** 2026-08-07
- **Reported version:** SRS `7.0.89`
- **Current checked version:** SRS `8.0.10`
- **Changes:** None

The original WHEP URL used `stream=test.flv`, but the published stream was `test`. The correct WHEP parameter is `stream=test`.

The reporter also tried the correct stream name but still received no media. Logs show that WHEP signaling, ICE, and DTLS succeeded.

SRS `7.0.89` had a known source-cleanup problem that could cause a publisher and player to use different source objects, resulting in playback without media. This was fixed in SRS `7.0.127` and later strengthened in SRS `8.0.5`.

The available evidence is consistent with that old defect, but does not prove it was the exact cause.

**Conclusion:** No new bug is confirmed in the current version. Please upgrade to a current SRS release, use `stream=test`, and report back with complete logs and publishing details if the problem remains.

## #4624 [FIXED] Live source removed during publisher activation

- **Issue:** https://github.com/ossrs/srs/issues/4624
- **Truth Record:** https://github.com/ossrs/srs/issues/4624#issuecomment-5234396855
- **Verified:** 2026-08-09
- **Changes:** None

SRS `v6.0-r0` had a live-source cleanup race during publisher activation. It is fixed in SRS `7.0.151+` and `8.0.5+`; affected users should upgrade to a current SRS release.

## #4628 [USAGE] Kick HLS viewers through the client API

- **Issue:** https://github.com/ossrs/srs/issues/4628
- **Truth Record:** https://github.com/ossrs/srs/issues/4628#issuecomment-5225936717
- **Verified:** 2026-08-08
- **Closure:** Expected behavior; issue closed with no project changes.

SRS already supports kicking HLS viewers: query `hls-play` clients through `/api/v1/clients/` and delete the client ID. An HLS viewer correctly reports `publish: false`; users should read the HLS and HTTP API documentation or ask SRS AI before opening documented-usage issues.

## #4627 [USAGE] RTMP does not support PCMA audio

- **Issue:** https://github.com/ossrs/srs/issues/4627
- **Truth Record:** https://github.com/ossrs/srs/issues/4627#issuecomment-5229356765
- **Verified:** 2026-08-08
- **Closure:** Unsupported usage; issue closed with no project changes.

SRS does not support PCMA/G.711 A-law in RTMP. Use AAC or MP3, transcode before publishing, or use G.711 only with the documented WebRTC WHIP/WHEP workflow.

## #4625 [BUG] MP4 DVR timeline inflated by repeated DTS samples

- **Issue:** https://github.com/ossrs/srs/issues/4625
- **Truth Record:** https://github.com/ossrs/srs/issues/4625#issuecomment-5233336576
- **Reported version:** SRS 6.0.184 (`63edbef90864d425a6303c64bae9600631a4c0f9`)
- **Verified branch/commit:** `forge` at `2f1f67a8125ae4051ed46e74143b15acc7a5ce54`
- **Current SRS version:** 8.0.10
- **Environment:** macOS 26.5.2 arm64; FFmpeg n8.1.1; ASAN C++ unit-test build
- **Supersedes:** None; this is the first Truth Record for this issue.

### Confirmed problem

The reporter's MP4 is internally inconsistent:

- MP4 header duration (`mvhd`/`tkhd`/`mdhd`/`elst`): **30.259 seconds**.
- Duration obtained by accumulating the video `stts` sample table: **140.099 seconds**.
- Video samples: **3,857**.
- Samples containing pictures: **964** (`31` IDR and `933` non-IDR).
- Auxiliary/non-picture samples: **2,893** (`1,928` SEI, `964` AUD, and `1` type-13 NAL).

This explains the observed behavior: VLC lists the file as approximately 30 seconds from the MP4 header, but playback follows the much longer sample timeline and reaches approximately 140 seconds. SEI/AUD-only samples do not decode a new picture, so the previous picture remains visible while their incorrect durations consume playback time.

### Root cause

SRS DVR records every RTMP video message as an MP4 sample. Consecutive messages from this stream can share the same DTS, for example a picture followed by separate SEI/AUD messages at the same timestamp. Their legitimate MP4 timing delta is therefore zero.

`SrsMp4SampleManager::write_track` used `sample_delta_ == 0` to determine whether the current `stts` entry was uninitialized. A real zero-duration entry was consequently mistaken for an empty entry. When the next positive DTS delta arrived, SRS replaced the zero delta instead of starting a new `stts` entry, assigning elapsed time to the accumulated same-DTS samples and inflating the playback timeline.

### Concrete timing example

This simplified example shows the failure mode. It illustrates the confirmed same-DTS condition; it is not a claim that the exact original RTMP sequence can be reconstructed from the MP4.

There are only two decoded video pictures:

| RTMP video message | H.264 content | DTS | Decoded picture? |
|---|---|---:|---|
| 1 | IDR picture | 0 ms | Yes |
| 2 | SEI metadata | 0 ms | No |
| 3 | AUD delimiter | 0 ms | No |
| 4 | P-picture | 30 ms | Yes |

SEI and AUD are H.264 auxiliary NAL units, not additional video frames. The IDR, SEI, and AUD messages belong to the same timestamp group, so the clock must not advance between them. The intended timeline is therefore:

```text
0 ms                              30 ms
|                                   |
IDR + SEI + AUD                     P-picture
```

The correct timing deltas are effectively:

```text
0 ms, 0 ms, 0 ms, 30 ms
```

Only one 30 ms interval passes between the two real pictures. However, the old `stts` builder treated the legitimate zero-delta entry as uninitialized. When the 30 ms delta arrived, it replaced the accumulated zero delta and effectively assigned 30 ms to each message:

```text
30 ms, 30 ms, 30 ms, 30 ms
```

That group consequently consumed about 120 ms instead of 30 ms. Repeating this pattern throughout the recording inflated the MP4 sample timeline even though the source DTS range, and therefore the MP4 header duration, remained approximately correct.

The original RTMP message timestamps are not independently preserved in the resulting MP4, so their exact original sequence cannot be reconstructed solely from this attachment. The malformed `stts` table and NAL distribution directly prove the file inconsistency, and a controlled RTMP reproduction confirms the same-DTS failure mode.

### Fix

Commit `2f1f67a8125ae4051ed46e74143b15acc7a5ce54` changes the `stts` initialization test to use `sample_count_ == 0`. This preserves zero as a valid DTS delta. It also adds regression test `ReproduceIssue4625.PreserveZeroDtsInMp4Stts`.

For the regression sequence `9, 9, 9, 9, 39` ms:

- Correct `stts`: `(1,9), (3,0), (1,30)`; total duration `39` ms.
- Before the fix: `(1,9), (4,30)`; total duration `129` ms.
- After the fix: the correct entries and duration are preserved.

### Runtime reproduction

A deterministic H.264/FLV sample was generated with four RTMP video packets per DTS and published through RTMP into SRS DVR MP4:

- Before the fix: MP4 header **28.920 seconds**, `stts` timeline **115.710 seconds**, 3,858 samples.
- After the fix: MP4 header **28.920 seconds**, `stts` timeline **28.920 seconds**, 3,858 samples.
- Post-fix entries preserve repeated zero deltas, such as `(4,0), (1,30), (3,0), (1,30)`.

### Verification

- Targeted ASAN unit test: passed.
- Full ASAN C++ unit suite: **2,191 tests passed** from 275 suites.
- Blackbox DVR tests passed:
  - `TestFast_RtmpPublish_DvrFlv_Basic`
  - `TestFast_RtmpPublish_DvrMp4_Basic`
- Controlled FFmpeg → RTMP → SRS DVR MP4 integration: passed; header and `stts` durations match after the fix.
- `git diff --check`: passed.

### Conclusion

The report is a confirmed SRS MP4 DVR timing bug caused by treating a legitimate zero DTS delta as an uninitialized `stts` entry. It is fixed and regression-tested in commit `2f1f67a8125ae4051ed46e74143b15acc7a5ce54` on the local `forge` branch.

The fix has not yet been merged or released. Confirmation against a fresh capture of the reporter's original RTMP input and confirmation in a released SRS version remain pending.

## #4623 [USAGE] Private origin address registered across unrelated networks

- **Issue:** https://github.com/ossrs/srs/issues/4623
- **Truth Record:** https://github.com/ossrs/srs/issues/4623#issuecomment-5234472135
- **Verified:** 2026-08-09
- **Closure:** Networking/configuration usage error; issue closed with no project changes.

The origin registered a private IP that the remote proxy could not reach. Set `SRS_DEVICE_IP` to an origin IP reachable by the proxy, and read the documentation or ask SRS AI for deployment guidance.

## #4620 [FEATURE] Generated names for SRT publishers without stream IDs

- **Issue:** https://github.com/ossrs/srs/issues/4620
- **Truth Record:** https://github.com/ossrs/srs/issues/4620#issuecomment-5241943782
- **Verified:** 2026-08-10
- **Closure:** Proposal rejected; issue closed as not planned with no project changes.

SRS intentionally uses the configured `default_streamid` when a publisher supplies no stream ID. A random per-connection name would be unknown to consumers and unstable across reconnections. Publishers must use stable, distinct stream IDs.

## #4617 [USAGE] Enable RTC-to-RTMP conversion

- **Issue:** https://github.com/ossrs/srs/issues/4617
- **Truth Record:** https://github.com/ossrs/srs/issues/4617#issuecomment-5242032033
- **Verified:** 2026-08-10
- **Closure:** Configuration usage issue; closed with no project changes.

RTC-to-RTMP conversion is disabled by default. Use `conf/rtc2rtmp.conf` or enable `rtc_to_rtmp`, and use the same vhost, app, and stream for WebRTC publishing and RTMP playback.

## #4616 [USAGE] Incorrect GB28181 Docker configuration

- **Issue:** https://github.com/ossrs/srs/issues/4616
- **Truth Record:** https://github.com/ossrs/srs/issues/4616#issuecomment-5247039175
- **Verified:** 2026-08-10
- **Closure:** Configuration usage error; closed with no project changes.

The configuration exposed GB28181 media port 9000 as UDP instead of TCP, used an unreachable hard-coded candidate, and omitted the final vhost brace; current SRS also requires an external SIP server.

## #4609 [BUG] Graceful disconnects inflated client error metrics

- Issue: https://github.com/ossrs/srs/issues/4609
- Truth Record: https://github.com/ossrs/srs/issues/4609#issuecomment-5247423361
- Verified: 2026-08-10
- Branch: `forge`, synchronized with `winlin/develop`
- Commit: `aff1e7d2b4457fc13a61e65e7b1dbec3c4f95afb`
- Version: SRS `8.0.14`; reproduced on SRS `6.0.185`
- Status: Fixed and tested on `winlin/develop`; not yet merged into `ossrs/srs:develop` or released

**Confirmed bug**

`SrsStatistic::on_disconnect()` counted every nonzero disconnect code as an error before the connection lifecycle classified graceful termination. This incorrectly increased `srs_clients_errs_total` for `ERROR_SOCKET_READ`, `ERROR_SOCKET_READ_FULLY`, `ERROR_SOCKET_WRITE`, `ERROR_SRT_IO`, and `ERROR_HTTP_STREAM_EOF`.

The fix excludes existing client- and server-graceful-close classifications while preserving genuine error counting. The regression test covers all five codes, and the final ASAN run passed 2249 tests from 291 suites. Runtime verification confirmed that normal RTMP player exits no longer increase `srs_clients_errs_total`.

**Unconfirmed claim**

The reported historical-maximum behavior of `srs_clients` was not reproduced; controlled tests returned it to the active-client count. Falling `srs_clients_total` values in the issue graph cannot originate from one uninterrupted verified SRS process and may involve restarts, multiple targets, or time-series configuration.

**Next action**

Merge the fix into `ossrs/srs:develop` and release it. If the `srs_clients` anomaly persists, collect the exact image digest, raw `/metrics`, PID/build samples, and Prometheus target and relabeling configuration.

## #4611 [BUG] HTTP-FLV on-demand playback regression

- **Issue:** https://github.com/ossrs/srs/issues/4611
- **Truth Record:** https://github.com/ossrs/srs/issues/4611#issuecomment-5247064760
- **Verified:** 2026-08-10
- **Closure:** Fixed in SRS 7.0.150 and 8.0.2 by PR #4678; issue closed with no additional project changes.

## #4622 [BUG] RTMP callback parameters duplicated when tcUrl is parsed twice

- **Issue:** https://github.com/ossrs/srs/issues/4622
- **Truth Record:** https://github.com/ossrs/srs/issues/4622#issuecomment-5240365106
- **Verified:** 2026-08-10
- **Fix commit:** `778623fa6459a7080847a5ddee83f1f43bd1eb48`
- **Version:** SRS `8.0.12`

### Confirmed problem

SRS parses an RTMP request during connection and again after identifying the publish stream. When `tcUrl` contains a query parameter, the second parsing could append that parameter again.

For example:

```text
Input:  ?token=abc
Output: ?token=abc?token=abc
```

Parameters supplied only through the publish stream work correctly. The defect occurs when parameters previously extracted from `tcUrl` are reused during the second parsing.

### Root cause

`srs_net_url_parse_tcurl()` uses `stream` and `param` as both inputs and outputs. On the second call, the old implementation rebuilt the URL from:

```text
tcUrl + identified stream + previously extracted param
```

It did not recognize that `param` was already contained in `tcUrl`. The HTTP callback merely serialized the resulting duplicated value.

### Conclusion

Issue #4622 is a confirmed SRS bug.

Commit `778623fa6459a7080847a5ddee83f1f43bd1eb48` makes URL reconstruction idempotent, avoids appending parameters already contained in `tcUrl`, preserves legacy RTMP URL compatibility, and adds regression coverage.

All 2,247 ASAN unit tests passed. Merge, release, whole-server callback verification, and reporter confirmation remain pending.

## #4621 [BUG] HTTP-FLV discarded the forwarded client IP

- **Issue:** https://github.com/ossrs/srs/issues/4621
- **Truth Record:** https://github.com/ossrs/srs/issues/4621#issuecomment-5241813445
- **Verified:** 2026-08-10
- **Branch/commit:** `develop` at `34f635a1af69aabaaa232598b8ad2b04da3dff63`
- **Version:** SRS `8.0.14`
- **Merged PR:** https://github.com/ossrs/srs/pull/4711
- **Supersedes:** None; first authorized Truth Record

### Confirmed problem

Nginx Proxy Manager sent `X-Forwarded-For` and `X-Real-IP`, but SRS displayed the proxy address for HTTP-FLV clients. `real_ip on;` is not an SRS configuration directive.

SRS parsed the forwarded address correctly, but `SrsLiveStream::serve_http_impl()` overwrote it with the proxy's TCP peer address before recording client statistics and applying playback security.

### Fix and verification

HTTP-FLV now uses the first `X-Forwarded-For` address, then `X-Real-IP`, and finally the TCP peer address as fallback. The same address is used for client statistics and playback security.

- Regression test `ReproduceIssue4621.PreserveForwardedIpForHttpFlvClient`: passed.
- Existing proxy-header parser test: passed.
- Full ASAN C++ unit suite: **2,248 tests passed**.
- Runtime reproduction confirmed `/api/v1/clients/` reports the forwarded address.

### Conclusion

The confirmed bug is fixed and merged in SRS 8.0.14. Only trusted reverse proxies should be allowed to supply forwarding headers; a configurable trusted-proxy allowlist remains outside this fix.
