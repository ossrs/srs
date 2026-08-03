# Issue Truth Records

Record only verified maintenance status and the latest maintainer-approved Truth Record. Never copy unverified issue discussion.

## #4690 — CURRENT

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

## #4686 — CURRENT

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

## #4684 — CURRENT

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

## #4681 — CURRENT

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

## #4671 — CURRENT

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

## #4663 — CURRENT

- Issue: https://github.com/ossrs/srs/issues/4663
- Truth Record: https://github.com/ossrs/srs/issues/4663#issuecomment-5151376223
- Verified: 2026-08-01; closed as a usage error with no project change

**Conclusion**

The publisher used stream `livestream`, but WHEP requested `livestream.flv`. WHEP treats `.flv` as part of the stream name; it is only appropriate for HTTP-FLV URLs. Use `stream=livestream`. This is user URL misuse, not an SRS bug, and the existing documentation is sufficient.

## #4656 — CURRENT

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
