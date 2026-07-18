# Issue Truth Records

Record only verified maintenance status and the latest maintainer-approved Truth Record. Never copy unverified issue discussion.

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
