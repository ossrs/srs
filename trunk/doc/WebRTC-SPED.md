# WebRTC SPED and DTLS 1.3

The design of SPED (STUN Protocol for Embedding DTLS) and DTLS 1.3 in the SRS WebRTC server, how to turn them on, how they are tested, and what they save.

- **SPED** puts the DTLS handshake inside the ICE STUN Binding requests and responses. It saves one round trip (RTT) on every path, and it recovers from a lost handshake packet at the pace of the ICE checks instead of the DTLS retransmission timer.
- **DTLS 1.3** needs one RTT for the handshake instead of two. SRS has it on an optional build with the bundled OpenSSL 4.1, and SPED works on it too.

Both are off by default. SPED is the server side of `draft-hancke-webrtc-sped-01`, an individual draft, and Chrome has it only behind a field trial. Treat both as experimental.

## Background

### Why WebRTC setup is slow

Before media flows, the protocols run one after another:

| Step | RTTs | Notes |
|---|---|---|
| Signaling (WHIP or WHEP HTTP offer and answer) | 1 | |
| ICE connectivity check (STUN Binding) | 1 | |
| DTLS 1.2 | 2 | The client sends its key share only after the server picks the group |
| DTLS 1.3 | 1 | The client guesses the group and sends `key_share` in its first ClientHello; a wrong guess costs a HelloRetryRequest and one RTT more |

- That is 4 RTTs to media with DTLS 1.2, and 3 with DTLS 1.3.
- With SPED, DTLS starts inside the first STUN check, so DTLS 1.2 takes 3 and DTLS 1.3 takes 2: one for signaling and one on the media path.
- WARP (`draft-uberti-tsvwg-warp-00`) bundles SPED, DTLS 1.3 and SNAP (SCTP in SDP) to take setup from 6 RTTs to 2. It recommends ICE Lite and `a=setup:passive` on servers, which is what SRS does already. SRS has no data channels, so SNAP does not apply.

### SRS's roles

- SRS is ICE Lite: it answers `a=ice-lite`, receives the checks and sends only responses. SRS never sends a Binding request.
- SRS answers an `actpass` offer with `passive`, so SRS is the DTLS server and the browser is the DTLS client. `vhost.rtc.dtls_role` can change this; SPED works in both roles.
- So with SPED, the browser's ClientHello rides in a Binding request and SRS's flight rides in the response to it.

### DTLS 1.2 flights with SRS as server

1. Client: ClientHello.
2. SRS: ServerHello, Certificate, ServerKeyExchange, CertificateRequest, ServerHelloDone.
3. Client: Certificate, ClientKeyExchange, CertificateVerify, ChangeCipherSpec, Finished.
4. SRS: ChangeCipherSpec, Finished.

On DTLS 1.3 the flights are ClientHello; ServerHello, then the encrypted EncryptedExtensions, CertificateRequest, Certificate, CertificateVerify and Finished; the client's Certificate, CertificateVerify and Finished; and SRS's DTLS 1.3 ACK record of the client's Finished.

### Status of the drafts and browsers

- SPED: `draft-hancke-webrtc-sped-01`, an individual draft discussed in AVTCORE, not adopted by a working group. Its codepoints are still TBD.
- Chrome: SPED behind the field trial `WebRTC-IceHandshakeDtls`, BoringSSL only. Chrome 158 was used for every test here. Its offer has `a=ice-options:trickle sped googspedv1` with the trial, and only `trickle` without it.
- DTLS 1.3 is on by default in Chrome, which falls back to DTLS 1.2.
- OpenSSL 4.1.0 is the first OpenSSL with DTLS 1.3. It is not an LTS release.

Links:

- SPED -01: <https://www.ietf.org/archive/id/draft-hancke-webrtc-sped-01.html>
- WARP -00: <https://www.ietf.org/archive/id/draft-uberti-tsvwg-warp-00.html>
- RFC 6347 (DTLS 1.2), RFC 9147 (DTLS 1.3), RFC 8446 (TLS 1.3), RFC 5764 (DTLS-SRTP), RFC 9443 (demultiplexing), RFC 8445 (ICE, Lite in section 2.5)

## Packet formats

### Negotiation

- The offer has `sped` in `a=ice-options`. Chrome puts its options on every media section.
- When `vhost.rtc.sped` is on and the offer has `sped` as a whole token, SRS answers `a=ice-options:sped` at the session level, which Chrome applies to every transport. `googspedv1` alone does not count.
- SPED is negotiated only when both sides have it. Otherwise the session is as before.

### The two STUN attributes

The draft's codepoints are TBD, so SRS uses Chrome's, which pion/stun uses too. They are named constants in one place, `SrsStunMessageAttribute` in `trunk/src/protocol/srs_protocol_rtc_stun.hpp`, so a later IANA assignment is a one-line change.

| Attribute | Type | Value |
|---|---|---|
| DTLS-IN-STUN-DATA | `0xC070` | One DTLS datagram, padded to 4 bytes. Sent only when it carries a datagram. A value whose first byte is not 20–63 is not DTLS and is dropped. |
| DTLS-IN-STUN-ACK | `0xC071` | A list of uint32 CRC-32 values, one per DTLS datagram received, embedded or raw, at most 4 (`kStunDtlsInStunMaxAcks`). An empty list is legal: Chrome's first check carries one. |

- Both attributes are comprehension-optional, so a server without SPED ignores them.
- MESSAGE-INTEGRITY authenticates them. SRS reads only the attributes before MESSAGE-INTEGRITY, so it never acts on unsigned data.
- The CRC is over the DTLS datagram's bytes, the DATA value without padding. It names the datagram, not the STUN transaction, so an ACK in any message covers every copy of that datagram.

### Example: browser to SRS, a Binding request with the ClientHello

```
00 01 | length | 21 12 a4 42 | 12-byte transaction ID
00 06 USERNAME "srsufrag:browserufrag"
00 24 PRIORITY
80 2a ICE-CONTROLLING (8-byte tie-breaker)
c0 71 DTLS-IN-STUN-ACK, length 0 (nothing received yet)
c0 70 DTLS-IN-STUN-DATA = one DTLS datagram:
      16 fe fd 0000 000000000000 len | 01 ClientHello ...
00 08 MESSAGE-INTEGRITY (HMAC-SHA1 with SRS's ICE password, covering the DTLS too)
80 28 FINGERPRINT
```

### Example: SRS to browser, the Binding success response

```
01 01 | length | 21 12 a4 42 | the same transaction ID
00 06 USERNAME
00 20 XOR-MAPPED-ADDRESS
c0 71 DTLS-IN-STUN-ACK = [crc32(ClientHello datagram)]
c0 70 DTLS-IN-STUN-DATA = the next datagram of SRS's flight
00 08 MESSAGE-INTEGRITY
80 28 FINGERPRINT
```

SRS writes the attributes in this order: USERNAME, XOR-MAPPED-ADDRESS, ACK, DATA, MESSAGE-INTEGRITY, FINGERPRINT.

### Sizes

- WebRTC keeps every UDP payload within 1200 bytes. SRS, like Chrome, keeps the STUN body before MESSAGE-INTEGRITY and FINGERPRINT within `1200 - 24 - 8 = 1168` bytes (`kStunSpedMaxBodySize`), and skips an embed that does not fit; that datagram still went raw.
- With SPED, SRS sets its DTLS MTU to 900 bytes (`kSpedDtlsMtu`), the value Chrome's field trial uses, so every datagram SRS writes fits a response with two 32-character ufrags and four ACKs.
- What Chrome 158 and SRS send, seen on the wire:

| Datagram | DTLS 1.2 | DTLS 1.3 (OpenSSL 4.1) |
|---|---|---|
| Chrome's ClientHello, with an X25519MLKEM768 key share | 900 + 563 B, two fragments | the same |
| SRS's first flight, default ECDSA certificate | one 707 B datagram | 899 + 900 + about 115 B |
| Chrome's last flight | one 577–580 B datagram | one 456 B datagram |
| SRS's last flight | 75 B ChangeCipherSpec and Finished | a 72 B ACK record |

- The DTLS 1.3 first flight is bigger because the ServerHello carries a 1120 B ML-KEM key share. On OpenSSL 4.1 SRS keeps OpenSSL's default groups, so it takes Chrome's first key share, X25519MLKEM768 or X25519, with no HelloRetryRequest.

## State machine

### Per session state

Each DTLS association, one per transport of a session, has one `SrsSpedController` (`trunk/src/app/srs_app_rtc_sped.{hpp,cpp}`). It has no network or OpenSSL code: the DTLS and STUN code feed it.

- **L1**, the current flight: the datagrams DTLS wrote for its last flight that the peer has not ACKed, with their CRCs. They are embedded one per response, round robin, and replaced when DTLS writes a new flight.
- **L2**, the ACK list: the CRCs of the DTLS datagrams received, embedded or raw, at most 4, oldest dropped first, no duplicates.

### States

The states are Chrome's:

| State | Meaning |
|---|---|
| tentative | SPED is negotiated in SDP, and the peer has not yet shown it in STUN. |
| confirmed | The peer sends SPED attributes, and the DTLS handshake is in progress. |
| pending | The DTLS handshake is done, and the last flight may still wait for an ACK. With SRS as DTLS 1.2 server, the session's players wait in this state. |
| complete | SPED is done. L1 and L2 are dropped, and later attributes are ignored. |
| off | The peer does not use SPED. DTLS goes raw only, as without SPED. |

Transitions:

- tentative → confirmed: the first authenticated message with either attribute.
- tentative → off: the first authenticated message with neither attribute.
- confirmed → pending: the DTLS handshake is done.
  - SRS as DTLS server keeps its last flight until the client ACKs it. On DTLS 1.2 that is ChangeCipherSpec and Finished; on DTLS 1.3 it is the ACK record of the client's Finished, which the client waits for.
  - SRS as DTLS client drops its last flight, which the server's last flight answers.
  - The ACK list is kept either way, since the peer waits for the ACK of its last flight.
- pending → complete: an ACK removes the last datagram of L1, a message has neither attribute, or an SRTP or SRTCP packet decrypts, which proves the peer finished DTLS.

### When media starts

A session starts its publishers and players when SRS's own DTLS handshake is done, with one exception for SPED:

- **SPED, DTLS 1.2, SRS as DTLS server:** the players start only when SPED is complete, that is when the viewer proved it finished DTLS by one of the three proofs above. SRS's handshake is done when it sends its last flight (ChangeCipherSpec and Finished), half a round trip before the viewer's. If that flight is lost, the viewer drops all SRTP until it has the flight again, so SRTP sent before the proof can be wasted. SRS logs `RTC: session sub=N wait for SPED complete` for such a session.
  - The publishers still start at done, since SRS only receives from them.
  - There is no packet cache and no timer: a player starts later and takes the media from that point, and a viewer that never proves it ends at the session timeout.
  - On a LAN with Chrome the wait is about 50–65 ms, until Chrome's next check or first SRTCP.
- **SRS as DTLS client, or a DTLS 1.3 server:** the players start at done. A DTLS 1.3 server is done only when it has the client's Finished, and the client sends it only once it has its own keys, so the client is ready first.
- **DTLS 1.2 without SPED:** the players start at done. After its Finished the client sends nothing SRS could wait for, so a lost last flight costs the client's resend timer, about 50–65 ms with Chrome: the client resends its last flight, SRS answers with its own again, and SRTP sent in that gap is dropped.

### What SRS does with a Binding request

For a session that negotiated SPED, `SrsRtcUdpNetwork::on_binding_request` and `SrsRtcTcpNetwork::on_binding_request` do the same, through two shared helpers in `trunk/src/app/srs_app_rtc_network.cpp`:

1. Start the DTLS handshake on the first check, before the response is built.
2. Take the request's ACKs, which prune L1, and its DATA. A DTLS datagram is fed to DTLS exactly as a raw one is, and DTLS may write SRS's next flight into L1.
3. Build the response with the ACK list and the next datagram of L1, if it fits.
4. Send it.

Without SPED the order is unchanged: the response goes first, then the handshake starts.

### Choices where the draft leaves one

SRS follows Chrome where the draft gives a choice:

- **Every flight is also sent raw,** as Chrome does. As ICE Lite, SRS has a valid pair once it has received a check, so on a lossless path the raw copy is often the one used, and the embedded copy is the fallback.
- **Raw DTLS is ACKed too,** so a flight that arrived raw stops being resent in STUN.
- **No implicit ACK.** Chrome counts a response's ACK attribute as covering the datagram its request embedded. SRS receives only requests, so it prunes L1 only by explicit CRCs.
- **Over TCP,** each raw datagram and each response is its own length-prefixed frame, and the same 1168 B body limit applies, so both transports send the same STUN messages.

### Code map

| Part | Where |
|---|---|
| Attributes, encode and decode | `SrsStunPacket` in `trunk/src/protocol/srs_protocol_rtc_stun.{hpp,cpp}` |
| `a=ice-options` parsing | `SrsSdp::has_ice_option()` in `trunk/src/protocol/srs_protocol_sdp.cpp` |
| Negotiation | `trunk/src/app/srs_app_rtc_api.cpp` and `SrsRtcConnection::generate_local_sdp` in `trunk/src/app/srs_app_rtc_conn.cpp` |
| Controller, L1, L2 and the states | `SrsSpedController` in `trunk/src/app/srs_app_rtc_sped.{hpp,cpp}` |
| DTLS hooks, timers and DTLS 1.3 | `SrsDtlsImpl` in `trunk/src/app/srs_app_rtc_dtls.{hpp,cpp}` |
| UDP and TCP binding paths, handshake simulator | `trunk/src/app/srs_app_rtc_network.{hpp,cpp}` |
| SRTP completing SPED | `SrsSecurityTransport` in `trunk/src/app/srs_app_rtc_conn.cpp` |
| Players waiting for SPED complete | `SrsRtcConnection::start_streams` and `on_sped_complete`, the controller's `ISrsSpedHandler`, in `trunk/src/app/srs_app_rtc_conn.cpp` |
| Clients API | `SrsStatisticClient::dumps` in `trunk/src/app/srs_app_statistic.cpp` |
| Config | `SrsConfig::get_rtc_sped()` in `trunk/src/app/srs_app_config.cpp` |

## Loss and timers

### How SPED recovers

SPED resends the same cached bytes in every response until the peer ACKs them. The peer's DTLS drops a repeat through its replay window, since the record numbers are the same.

| What is lost | What happens | Cost |
|---|---|---|
| The browser's first check | The next check, about 50 ms later, carries the same ClientHello | one check interval |
| The response carrying SRS's flight | The next check has no ACK of it, so SRS embeds the same flight again; OpenSSL drops the repeated ClientHello and writes nothing | one check interval |
| The browser's last flight | It is embedded again until ACKed, or its raw copy arrives and SRS ACKs it | one check interval |
| SRS's last flight | Kept in L1 and embedded in later responses until ACKed | one check interval |

Chrome sends a new check about every 48 ms while no pair is writable, each with a new transaction ID, so this is the pace of recovery. A fragmented flight works the same way, one datagram per response.

### DTLS timers while SPED is active

The draft says DTLS timers SHOULD be off while SPED is active, and Chrome sets its timer to 24 hours until it has an ICE RTT.

- **DTLS 1.2, and SRS as DTLS client:** while SPED is active, the OpenSSL timer callback returns its 30 s maximum. Otherwise OpenSSL's 50 ms timer expires before the next check, and its retransmission, in more datagrams, replaces the flight SPED resends; tests showed every loss then cost several round trips. OpenSSL still resends ChangeCipherSpec and Finished at once on a retransmitted client Finished. Once SPED is complete or off, the timer is as before: 50 ms, doubling to 30 s.
- **DTLS 1.3 server:** the timer stays on, with SPED too. A DTLS 1.3 client that ACKed part of the server's flight waits for the server's timer to resend the rest (RFC 9147 section 7.2), and BoringSSL drops encrypted records that arrive before the ServerHello, though SPED has ACKed them by CRC. So SRS as a DTLS 1.3 server starts a timer coroutine with its first flight and resends what the client has not ACKed. The timer's copy replaces SPED's flight only when the client has ACKed every datagram of it in STUN; before that, it goes raw only, and SPED keeps resending its flight.

### OpenSSL 4.1.0's DTLS 1.3 server

- OpenSSL 4.1.0's DTLS 1.3 server aborts with a fatal `unexpected_message` alert when a client ACKs part of the server's first flight, which BoringSSL does after a loss (openssl/openssl#33242). An OpenSSL client never sends that ACK, so OpenSSL-to-OpenSSL does not show it.
- The bundled `trunk/3rdparty/openssl-4-fit` carries the upstream fix for 4.1.1, as `trunk/3rdparty/README.md` records. A `--sys-ssl=on` build against a system OpenSSL 4.1.0 does not have the fix.
- An existing OpenSSL 4.1 build cache keeps the old library: remove `objs/Platform-*-SSL4/3rdparty/openssl` to rebuild it.

## Config and build

### SPED

```
vhost __defaultVhost__ {
    rtc {
        enabled on;
        # Answer a=ice-options:sped when the offer has it. Default: off.
        sped on;
    }
}
```

- The env `SRS_VHOST_RTC_SPED=on` sets it for all vhosts.
- A value other than `on` or `off` is refused at startup.
- SPED changes only the sessions whose offer has `sped`; every other session is as before.

### DTLS 1.3

DTLS 1.3 needs OpenSSL 4.1 or newer. The default build stays on OpenSSL 1.1.1 and DTLS 1.2.

```bash
./configure --ssl-4=on && make
```

- `--ssl-4=on` builds the bundled OpenSSL 4.1 in `trunk/3rdparty/openssl-4-fit`, with its own third-party cache, so switching between the two builds is a reconfigure and `make`. It is refused with `--ssl-1-0=on` or `--sys-ssl=on`.
- The bundled tree is built with only the features SRS, libsrtp and libsrt use, with assembly as the default build is (`-no-asm` on Loongson and with `--nasm=off`), and holds only the files those builds read with and without assembly, plus the license and five files Configure checks exist: 1498 files and 649,556 lines, 102 files and 157,780 lines of them assembly and perlasm, of the release's 2506 and 1,051,667 after its tests and docs. `trunk/3rdparty/README.md` has the steps to trim a new 4.x release, with the scripts in `trunk/3rdparty/openssl-4-trim`.
- Measured on an Apple M4 (Linux arm64 in Docker for the CPU), both builds with assembly: one WHIP stream to 1000 WHEP players costs 38.6% of a core on the 4.1 build against 38.0% on the default build (+1.7%, paired per second over 120 s), and a DTLS 1.3 handshake with SPED takes the server 1 ms, as before the trim. The trim cuts the cold OpenSSL build from 23–24 to 17 s on macOS and from 17–19 to 11–12 s on Linux, configure included, and `libcrypto.a` from 9.0 to 6.0 MB on macOS arm64.
- `--sys-ssl=on` against a system OpenSSL 4.1 or newer gets DTLS 1.3 too.
- The native Windows build uses vcpkg's OpenSSL and stays on DTLS 1.2.

The DTLS version is `vhost.rtc.dtls_version`, or the env `SRS_VHOST_RTC_DTLS_VERSION`:

| Value | OpenSSL 4.1 build | Other builds |
|---|---|---|
| `auto` (default) | the newest both sides have, up to DTLS 1.3 | DTLS 1.2 |
| `dtls1.3` | DTLS 1.3 only | refused at startup |
| `dtls1.2` | DTLS 1.2 only | DTLS 1.2 only |
| `dtls1.0` | DTLS 1.0 only | DTLS 1.0 only |

### Observing it

- One trace line per session when SPED ends, such as `RTC: SPED complete, embedded sent=0, embedded recv=1, raw recv=2, handshake=2ms`.
- `RTC: session sub=N wait for SPED complete` when a session's players wait for the viewer's proof.
- The clients API, `/api/v1/clients/`, has a `sped` object for an RTC client whose session negotiated SPED: `state`, `embedded_sent`, `embedded_recv`, `raw_recv` and `handshake_ms` (from the first STUN message to DTLS done, -1 until then).
- `/api/v1/features` has `dtls13`, true on a build with DTLS 1.3, and `handshake_drop`, true on a `--simulator=on` build.

### Trying it with Chrome

Start Chrome with the field trial:

```bash
google-chrome --force-fieldtrials=WebRTC-IceHandshakeDtls/Enabled/
```

Then publish over WHIP or play over WHEP against an SRS with `sped on`. The clients API shows `"sped": {"state": "complete", ...}`, and `chrome://webrtc-internals` shows the DTLS version (`tlsVersion` `FEFC` for DTLS 1.3, `FEFD` for DTLS 1.2).

## Tests

### Unit tests

In `trunk/src/utest/srs_utest_ai50.cpp` (STUN, SDP, config), `srs_utest_ai51.cpp` (controller, DTLS hooks, UDP and TCP paths, loss, stats, simulator) and `srs_utest_ai52.cpp` (OpenSSL 4.1 and DTLS 1.3). The DTLS tests run SRS's DTLS against an in-process OpenSSL client, with no sockets:

```bash
bash skills/srs-develop/scripts/srs-utest-run.sh --configure '*Sped*:*Dtls13*:RtcHandshakeDropTest.*:OpenSsl*'
```

- The loss cases: a lost response, a lost client flight, fragmented flights at a 256 B DTLS MTU, two candidate pairs sharing one session, and SRTP while pending.
- When media starts: each proof starting the waiting players once, publishers, SRS as DTLS client and DTLS 1.3 starting at done, a lost DTLS 1.2 last flight without SPED, and a lost DTLS 1.3 server flight, with and without SPED.
- The DTLS 1.3 cases run only on a `--ssl-4=on` build: `srs-utest-run.sh --configure --ssl-4` switches to it, and `--configure` alone switches back. `bash skills/srs-develop/scripts/srs-utest-asan-linux.sh --ssl-4` runs them on Linux in Docker.
- They include a test-written DTLS 1.3 ACK record, as a browser sends after a loss, and the server's timer resending the rest of its flight.

### Integration tests

The raw WebRTC client in `cmd/integ/internal/rtc` has a SPED mode (`cmd/integ/internal/rtc/sped.go`): it offers `sped`, embeds DTLS in checks every 50 ms with ACKs, also sends raw once the pair is writable, uses a 900 B DTLS MTU and falls back to off. Its faults (`cmd/integ/internal/rtc/faults.go`) lose chosen or random STUN and DTLS datagrams each way and add a one-way delay. It is DTLS 1.2 only, as pion/dtls is.

```bash
go -C cmd/integ/cases test -count=3 -run 'TestRtc_Sped'
```

The cases in `cmd/integ/cases/rtc_sped_test.go`, part of `make integ`:

| Case | Checks |
|---|---|
| PublishPlayInStun | SPED complete on the client and in the clients API, ClientHello and SRS's first flight embedded, media plays |
| SpedOffFallsBack | the default `sped off` answers no `sped`; the client falls back; media plays |
| LostFirstResponse | the first response and every raw DTLS from SRS lost; both of SRS's flights arrive inside STUN |
| LostClientFlight | the check with the client's flight and every raw client DTLS lost; SRS gets it inside STUN |
| SimulatorLosesFirstResponse | the handshake simulator loses SRS's first response, on a `--simulator=on` build |
| SimulatorLosesLastFlight | without SPED, the handshake simulator loses SRS's DTLS 1.2 last flight; the client resends its own, SRS answers it again |
| PlayerStartsOnSpedComplete | a player of a live stream with SRS's DTLS 1.2 last flight lost raw and in its first response; no media before the player has that flight |
| TcpPublishPlayInStun | the first case, over WebRTC TCP |
| TcpRawDtlsLost | every raw DTLS lost both ways, over TCP |
| TcpSimulatorLosesLastFlight | the lost last flight, over TCP |
| TcpPlayerStartsOnSpedComplete | the player of a live stream, over TCP |
| RandomLoss | 20 sessions at 25% STUN and DTLS loss, all complete and play |

### Handshake simulator

A `--simulator=on` build takes `drop-stun=<n>`, `drop-dtls=<n>` and `drop-finished=<n>` in the WHIP or WHEP URL: SRS loses the next n STUN Binding responses, the next n DTLS datagrams, and the next n datagrams of its DTLS 1.2 last flight (those with a ChangeCipherSpec or an epoch 1 handshake record) that it sends the session, over UDP and TCP. A lost last flight is the one a client answers with a resend of its own last flight, which SRS answers at once. Arming it with the offer is race-free: a browser starts its checks within a few ms of the answer, before a later API call could reach the session.

### Chrome tests

```bash
bash skills/srs-develop/scripts/chrome-sped-test.sh
```

- Chrome with the field trial publishes over WHIP and plays over WHEP on a `sped on` SRS, on the same SRS with `drop-stun=1`, and on a `sped off` SRS, which on DTLS 1.2 also takes two sessions with `drop-finished=1`.
- Checks: SPED complete in the clients API, ClientHello embedded, and after the lost response SRS's first flight embedded too; no SPED with `sped off`; frames encoded and decoded; every job's DTLS version is `FEFC` when `/api/v1/features` has `dtls13`, else `FEFD`.
- A browser whose offer has no `sped` with the trial is a `SKIP`; without `--simulator=on` only the drop part is.
- It runs in lane I of the Full tier, after the other Chrome cases, on the Mac and in the Linux and Windows Chrome runners.

### Measurement

```bash
bash skills/srs-develop/scripts/sped-measure.sh
bash skills/srs-develop/scripts/chrome-sped-measure.sh
```

- `sped-measure.sh` runs `srs-integ measure` (`cmd/integ/measure.go`) against one `sped on` SRS. Raw WHEP players offer `sped` or not, so one server gives both modes. Each run is timed from posting the offer to DTLS done, and to the first RTP packet, over 0/20/60/150 ms one-way delay (signaling too) and 0/10/25% STUN and DTLS loss each way, 30 runs per point. It prints p50, p95 and the failures per point.
- `chrome-sped-measure.sh` starts a new Chrome per run, publishing over WHIP and playing over WHEP, with the trial off and on alternating, 20 runs each. It times `setRemoteDescription` to `connected` and to the player's first frame, at zero loss.

## Results

Measured on an Apple M4 Mac (macOS 27.2.1, arm64) with Chrome 158.0.8127.37 and go1.27.0, SRS 8.0.77 with this feature, built with `--srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on` and the default ECDSA certificate. The sanitizer build adds a few ms of CPU per handshake, the same in both modes.

### DTLS 1.2, Go client

The command is `sped-measure.sh` with no flags, OpenSSL 1.1.1l. The loss is of STUN and DTLS only, each way, not of signaling or media. Times are ms from posting the offer; percentiles over the runs that succeeded.

| One-way delay | Loss | SPED | Fails of 30 | DTLS p50 | DTLS p95 | First RTP p50 | First RTP p95 |
|---|---|---|---|---|---|---|---|
| 0 ms | 0% | off | 0 | 4 | 5 | 18 | 20 |
| 0 ms | 0% | on | 0 | 5 | 5 | 18 | 20 |
| 0 ms | 10% | off | 0 | 54 | 7008 | 59 | 7019 |
| 0 ms | 10% | on | 0 | 8 | 58 | 18 | 77 |
| 0 ms | 25% | off | 4 | 1011 | 10020 | 1014 | 10037 |
| 0 ms | 25% | on | 0 | 60 | 208 | 76 | 218 |
| 20 ms | 0% | off | 0 | 183 | 187 | 195 | 199 |
| 20 ms | 0% | on | 0 | 138 | 144 | 154 | 158 |
| 20 ms | 10% | off | 0 | 182 | 3173 | 198 | 3199 |
| 20 ms | 10% | on | 0 | 136 | 189 | 157 | 200 |
| 20 ms | 25% | off | 2 | 2175 | 10286 | 2203 | 10286 |
| 20 ms | 25% | on | 0 | 144 | 251 | 161 | 267 |
| 60 ms | 0% | off | 0 | 502 | 510 | 516 | 532 |
| 60 ms | 0% | on | 0 | 384 | 392 | 394 | 422 |
| 60 ms | 10% | off | 1 | 499 | 3498 | 526 | 3520 |
| 60 ms | 10% | on | 0 | 382 | 453 | 396 | 462 |
| 60 ms | 25% | off | 4 | 1654 | 18841 | 1675 | 18841 |
| 60 ms | 25% | on | 0 | 404 | 527 | 415 | 537 |
| 150 ms | 0% | off | 0 | 1230 | 1237 | 1245 | 1261 |
| 150 ms | 0% | on | 0 | 918 | 930 | 940 | 950 |
| 150 ms | 10% | off | 0 | 1224 | 2231 | 1239 | 2240 |
| 150 ms | 10% | on | 0 | 918 | 1014 | 940 | 1033 |
| 150 ms | 25% | off | 2 | 2215 | 9273 | 2230 | 9273 |
| 150 ms | 25% | on | 0 | 966 | 1085 | 970 | 1094 |

Every failure is a run without SPED whose handshake passed the 30 s timeout; SPED had none of 360 runs.

- **One RTT saved on a lossless path.** Without SPED, DTLS is done after about 4 RTTs; with it, about 3. At 20, 60 and 150 ms one-way: 183 vs 138 ms, 502 vs 384 ms, 1230 vs 918 ms, saving 45, 118 and 312 ms. The 20–30 ms above the RTT count is the client's 50 ms check timer and CPU time.
- **Under loss the gap is far wider.** With SPED the p95 stays within 1–2 RTTs of the lossless time. Without it, the p95 is pion's DTLS backoff (1 s, doubling), 2.2–7 s at 10% loss and 9–19 s at 25%, and 7–13% of the 25% runs time out, so on all runs that p95 is past 30 s.

### DTLS 1.2, Chrome at zero loss

The command is `chrome-sped-measure.sh`, three runs of 20 pairs, candidate the Mac's LAN address, so the RTT is near 0. ms, p50/p95:

| Trial | Job | Connected | First frame |
|---|---|---|---|
| off | WHIP | 2/3 | - |
| off | WHEP | 2/2 | 175–180 / 190 |
| on | WHIP | 2/3 | - |
| on | WHEP | 2/2 | 174–184 / 188–191 |

SRS completed SPED for every trial session. At an RTT near 0 the RTT SPED saves is below the 1 ms resolution. The first frame, about 175 ms in both modes, is the wait for a keyframe: the RTC source has no GOP cache, so a player waits for the publisher's encoder to answer SRS's PLI.

With players waiting for SPED complete ("When media starts"), the same command on the same host, one run of 20 pairs before that change and one after:

| Trial | Job | Connected, before / after | First frame, before / after |
|---|---|---|---|
| off | WHEP | 2/5, 2/3 | 184/197, 187/195 |
| on | WHEP | 2/3, 2/3 | 187/199, 184/197 |

- Each trial player waited 48–65 ms (p50 56) from SRS's DTLS done to SPED complete, from SRS's log.
- Neither the player's `connected` nor its first frame moved beyond the run-to-run spread. `connected` is the browser's own DTLS done, and the wait likely overlaps the keyframe wait above, which is longer.

### DTLS 1.3, Chrome at zero loss

The same command on a `--ssl-4=on` build with `dtls_version auto`, 20 pairs. Every job negotiated DTLS 1.3, with and without the trial, and SRS completed SPED for all 40 trial sessions.

| Trial | Job | Connected | First frame |
|---|---|---|---|
| off | WHIP | 2/3 | - |
| off | WHEP | 2/2 | 173/188 |
| on | WHIP | 2/3 | - |
| on | WHEP | 2/2 | 179/189 |

- The same as DTLS 1.2 at an RTT near 0, as expected. The Go client cannot measure DTLS 1.3 under delay and loss, as pion/dtls has only DTLS 1.2.
- With SPED, a lost first STUN response costs about 50 ms on DTLS 1.2 and about 100 ms on DTLS 1.3 (the server's 50 ms timer plus a check), in `chrome-sped-test.sh`'s drop part.

### Against the draft

The draft reports, at 200 ms RTT and 25% loss, a DTLS 1.2 p95 of 8860 ms without SPED and 1400 ms with it. Our 60 ms and 150 ms one-way rows (120 and 300 ms RTT) bracket that RTT:

| | Draft, 200 ms RTT | SRS, 120 ms RTT | SRS, 300 ms RTT |
|---|---|---|---|
| Without SPED, p95 | 8860 ms | 18841 ms over successes, 4 of 30 timed out | 9273 ms over successes, 2 of 30 timed out |
| With SPED, p95 | 1400 ms | 527 ms | 1085 ms |

- The direction and size agree: SPED cuts the 25%-loss p95 from many seconds to about one second.
- The SRS p95 with SPED is lower than the draft's, likely because our loss spares signaling and media, SRS's DTLS 1.2 first flight is a single datagram, and our client checks every 50 ms. These reasons were not verified against the draft's setup.
- Without SPED, a client's resend reaches SRS's DTLS 1.2 server, which answers only when its own OpenSSL timer has expired too, which makes the tail without SPED long. SPED sessions do not see it.

## Limits and future work

- SPED and DTLS 1.3 are off by default; making them the default, and OpenSSL 4.1 the default build, are later decisions.
- DTLS 1.3 is not on the native Windows build.
- SRS sends SRTP only after its DTLS handshake is complete, and to a SPED viewer on DTLS 1.2 only after the viewer proved it finished DTLS, as "When media starts" says. First-frame strategies are not done: a keyframe cache for RTC players, sending media before the client's Finished (0.5-RTT data), redundant copies of the server flight, and restarting from a keyframe after a lost flight.
- SNAP (SCTP in SDP) does not apply, as SRS has no data channels.
