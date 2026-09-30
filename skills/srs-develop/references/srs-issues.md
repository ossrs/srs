# SRS Issue Truth Records

Record only verified `ossrs/srs` maintenance status and the latest maintainer-approved Truth Record. Never copy unverified issue discussion. Keep Oryx records in `references/oryx-issues.md`.

## #4739 [BUG] SRT multi-level stream IDs could not play over HTTP-FLV/TS

- Issue: https://github.com/ossrs/srs/issues/4739
- Truth Record: https://github.com/ossrs/srs/issues/4739#issuecomment-5737675082
- Status: Fixed on `forge` (PR #4743, `8.0.37`), pending review

SRT split the stream ID at the first `/`, so HTTP-FLV/TS viewers of multi-level streams hung. Every protocol now uses the last segment as the stream and the rest as the app.

## #4738 [BUG] WebRTC play answers H.264 for an H.265 stream

- Issue: https://github.com/ossrs/srs/issues/4738
- Truth Record: https://github.com/ossrs/srs/issues/4738#issuecomment-5723763184
- Status: Fixed on `develop` (`8.0.36`), pending review

Without `?vcodec=`, WebRTC play answered H.264 for an H.265 stream and sent no video: the answer followed the placeholder-track order, while the RTMP-to-RTC bridge used the H.265 track. Affects `7.0.44`+ with `rtmp_to_rtc on`; workaround `?vcodec=h265`. Now the publishing stream's codec, recorded in `bridge_video_codec_`, decides, and a conflicting request is refused.

## #4728 [FEATURE] Optional H.264 SEI stripping for Oryx Virtual Live

- Issue: https://github.com/ossrs/srs/issues/4728
- Truth Record: https://github.com/ossrs/srs/issues/4728#issuecomment-5462820007
- Status: Oryx enhancement proposed; no change yet

No SRS defect. Proposed: an optional Oryx Virtual Live setting that strips SEI with `-bsf:v filter_units=remove_types=6`, off by default. No reproducible sample yet.

## #4727 [SECURITY] Bundled SRT 1.5.3 exposed CVE-2026-55868/55869 stack overflows

- Issue: https://github.com/ossrs/srs/issues/4727
- Truth Record: https://github.com/ossrs/srs/issues/4727#issuecomment-5356428807
- Fix: PR #4729, SRS `8.0.29`
- Status: Fix pending merge; no v7 or v6 backport

In vendored SRT 1.5.3, any peer that completed a handshake could overflow a 104-byte stack buffer through post-connect key-material messages (`UMSG_EXT`), with no passphrase needed. Only deployments with `srt_server` enabled (off by default) were exposed. Fixed by vendoring upstream 1.5.6; the only local change is `trunk/3rdparty/patches/srt/api.cpp-01.patch`, which should be regenerated on each upgrade.

## #4719 [BUG] External-SIP GB28181 sessions remain reserved after media termination

- Issue: https://github.com/ossrs/srs/issues/4719
- Truth Record: https://github.com/ossrs/srs/issues/4719#issuecomment-5288512580
- Fix: PR #4721, SRS `8.0.22`
- Status: Fix pending merge; no v6 backport

Closing a GB28181 media TCP connection did not end its session, so the stream ID and SSRC stayed reserved; API sessions that never connected also stayed forever. Now the current media disconnect ends the session, and unconnected sessions expire after `stream_caster.media_connect_timeout` (10s default). There is no unpublish API, and SIP BYE does not reach SRS.

## #4697 [FEATURE] RTC audio pause/resume compatibility

- Issue: https://github.com/ossrs/srs/issues/4697
- Truth Record: https://github.com/ossrs/srs/issues/4697#issuecomment-5242316171
- Status: Closed as not planned

Some `flutter_webrtc` desktop clients send no audio RTP while muted, so resumed audio lags video by the pause length and HLS gets one long segment. SRS won't add timestamp heuristics for this; publishers should send silence, negotiate DTX, or keep the RTP/RTCP timeline.

## #4690 [SECURITY] Unauthenticated proxy registration endpoint

- Issue: https://github.com/ossrs/srs/issues/4690
- Truth Record: https://github.com/ossrs/srs/issues/4690#issuecomment-5125571911
- Status: Open; no short-term change

`/api/v1/srs/register` has no authentication by design, and a port-only `PROXY_SYSTEM_API` listens on all interfaces, so anyone who reaches it can register a backend and receive new streams. Workaround: bind `PROXY_SYSTEM_API=127.0.0.1:12025` behind an authenticated proxy, or firewall it. Built-in authentication is a future feature.

## #4686 [FEATURE] Media over QUIC support

- Issue: https://github.com/ossrs/srs/issues/4686
- Truth Record: https://github.com/ossrs/srs/issues/4686#issuecomment-5004294564
- Status: Open; deferred

SRS doesn't support MoQ, and the spec is still a draft. New protocols won't go into the C++ server; MoQ comes after the Go proxy, Go origin, and Go edge.

## #4684 [BUG] Blocking DNS resolution stalls State Threads

- Issue: https://github.com/ossrs/srs/issues/4684
- Truth Record: https://github.com/ossrs/srs/issues/4684#issuecomment-5011850798
- Status: Open; no fix

`srs_tcp_connect()` calls blocking `getaddrinfo()`, which stalls every coroutine on the business thread; the connect timeout doesn't cover it. Forwarding to a domain name triggers it. Non-blocking DNS (#2112) was never implemented. Workaround: use IP addresses, or send hooks to a local sidecar on `127.0.0.1` that resolves the domain.

## #4681 [FEATURE] Enhanced RTMP v2 multitrack audio

- Issue: https://github.com/ossrs/srs/issues/4681
- Truth Record: https://github.com/ossrs/srs/issues/4681#issuecomment-5902547777
- Status: Closed as not planned, 2026-09-29

SRS keeps one audio track per stream and rejects `SoundFormat=9`. Multitrack audio isn't planned; Enhanced RTMP Opus would come first if Enhanced RTMP audio is added.

## #4671 [BUG] Incomplete WebRTC RTX handling

- Issue: https://github.com/ossrs/srs/issues/4671
- Truth Record: https://github.com/ossrs/srs/issues/4671#issuecomment-5151313612
- Status: Open; handle with PR #4644

Same-SSRC NACK retransmission works, but RFC 4588 RTX on a separate SSRC doesn't: FID association fails (`rtx_ssrc_ = 0`), RTX packets aren't unwrapped, and the WHIP answer doesn't advertise RTX. PR #4644 covers the first two but is open and conflicting. Don't use the early `is_active_ = true` workaround as the fix.

## #4663 [USAGE] WHEP stream name incorrectly includes `.flv`

- Issue: https://github.com/ossrs/srs/issues/4663
- Truth Record: https://github.com/ossrs/srs/issues/4663#issuecomment-5151376223
- Status: Closed as usage error

WHEP requested `livestream.flv` for stream `livestream`; use `stream=livestream`. `.flv` belongs only in HTTP-FLV URLs.

## #4656 [BUG] Live-source cleanup can separate publishers and players

- Issue: https://github.com/ossrs/srs/issues/4656
- Truth Record: https://github.com/ossrs/srs/issues/4656#issuecomment-5161223806
- Status: Candidate fix, uncommitted and unreleased as of 2026-08-02

New players get `active=0` while the publisher and HLS keep working. The publisher holds the publish token and fetches source A, then yields before `acquire_publish()`; cleanup erases the inactive A, and new players create an empty source B. Fix: cleanup skips a dead source while its publish token is acquired (`ReproduceIssue4656.PublishTokenKeepsPendingLiveSource`).

## #4647 [FEATURE] Configurable proxy origin registration TTL

- Issue: https://github.com/ossrs/srs/issues/4647
- Truth Record: https://github.com/ossrs/srs/issues/4647#issuecomment-5173896319
- Status: Fixed in `8.0.6` (PR #4694)

`PROXY_ORIGIN_SERVER_TTL` (Go duration, default 300s) sets the origin registration lifetime for both memory and Redis load balancers. Only this TTL is configurable; the 120s HLS and WebRTC session caches stay fixed.

## #4646 [FEATURE] Proxy origin and stream-mapping query APIs

- Issue: https://github.com/ossrs/srs/issues/4646
- Truth Record: https://github.com/ossrs/srs/issues/4646#issuecomment-5180281566
- Status: Open; deferred

The proxy has no API to list registered origins or stream-to-origin mappings. Useful, but it needs careful API design first.

## #4645 [BUG] Browser player URLs fail behind reverse proxies

- Issue: https://github.com/ossrs/srs/issues/4645
- Truth Record: https://github.com/ossrs/srs/issues/4645#issuecomment-5180983219
- Status: Candidate fix, uncommitted and unreleased as of 2026-08-04

Behind a reverse proxy, the browser player built media URLs on port 8080 (HTTP) or 1935 (HTTPS). `trunk/research/players/js/srs.page.js` now uses the page's protocol and port by default, and explicit query overrides still win. Test: `node skills/srs-develop/scripts/browser-page-url-test.js`. No real reverse-proxy playback test yet.

## #4642 [BUG] Duplicate WebRTC TCP owner can leave a stale session pointer

- Issue: https://github.com/ossrs/srs/issues/4642
- Truth Record: https://github.com/ossrs/srs/issues/4642#issuecomment-5207071995
- Status: Fixed on `forge` (`5f4d5c9a4`), not merged or released

The issue's explanation is wrong, but a real bug exists: a second WebRTC-over-TCP connection for the same session replaced the first owner before the uniqueness check, leaving the first with a stale `session_` that could crash at `session_->tcp()`. Now a second owner is rejected before any state changes (`ReproduceIssue4642.RejectSecondTcpConnForSameRtcSession`). Whether the reporter's crash followed this path is unconfirmed.

## #4641 [USAGE] IPv6 RTMP listener requires explicit configuration

- Issue: https://github.com/ossrs/srs/issues/4641
- Truth Record: https://github.com/ossrs/srs/issues/4641#issuecomment-5203763730
- Status: Open; awaiting reporter

IPv6 RTMP works since `7.0.67` with `rtmp { listen [::]:1935; }`. The report has no version, config, or logs, so no bug is established.

## #4634 [USAGE] Classic edge does not support WebRTC

- Issue: https://github.com/ossrs/srs/issues/4634
- Truth Record: https://github.com/ossrs/srs/issues/4634#issuecomment-5216829576
- Status: Closed as expected behavior

Classic edge supports RTMP and HTTP-FLV, not WebRTC, as documented.

## #4633 [USAGE] Configure HTTP-FLV header track flags

- Issue: https://github.com/ossrs/srs/issues/4633
- Truth Record: https://github.com/ossrs/srs/issues/4633#issuecomment-5216986292
- Status: Open; no project change

`0x01` is the video-only HTTP-FLV header flag. To advertise both tracks from the first header, set `http_remux { has_audio on; has_video on; guess_has_av off; }`.

## #4632 [USAGE] Origin RTMP handshake timeout keeps the edge publisher busy

- Issue: https://github.com/ossrs/srs/issues/4632
- Truth Record: https://github.com/ossrs/srs/issues/4632#issuecomment-5217342826
- Status: Closed; upstream handshake timeout

The origin didn't finish the RTMP handshake within 30s; meanwhile the edge correctly rejects another publisher for the same stream as busy. No SRS defect.

## #4631 [BUG] Forward backend failure leaves the live source busy

- Issue: https://github.com/ossrs/srs/issues/4631
- Truth Record: https://github.com/ossrs/srs/issues/4631#issuecomment-5218171343
- Status: Fixed on `forge` (`70309a6a2`), not merged or released

A dynamic-forward backend error such as HTTP 500 rejected the publish but left the source marked publishing, so later publishers got `StreamBusy` until restart. Now publish state is released after a failed acquire, except for `StreamBusy`. Workaround on released versions: return `code: 0` with empty `urls`.

## #4639 [BUG] Missing CRLF after SDP SSRC group

- Issue: https://github.com/ossrs/srs/issues/4639
- Truth Record: Pending maintainer publication
- Status: Candidate fix staged on `forge`

`SrsSSRCGroup::encode()` wrote no line terminator, so `a=ssrc-group:` ran into the next SDP line. Fixed by appending `kCRLF` (`ProtocolSdpTest.SrsSSRCGroupEncode*`). The normal `SrsMediaDesc::encode()` path doesn't seem to emit SSRC groups, so the reporter's runtime path is unclear.

## #4629 [USAGE] GB28181 uses one shared media listener

- Issue: https://github.com/ossrs/srs/issues/4629
- Truth Record: https://github.com/ossrs/srs/issues/4629#issuecomment-5223834511
- Status: Closed as expected behavior

SRS has one GB28181 media listener by design; all cameras use that port. The old `sip.listen` was the embedded SIP server; current SRS needs an external one.

## #4626 [UNCONFIRMED] WHEP playback has no media on an old SRS version

- Issue: https://github.com/ossrs/srs/issues/4626
- Truth Record: https://github.com/ossrs/srs/issues/4626#issuecomment-5229451675
- Status: Open; awaiting reporter

On `7.0.89`, WHEP used `stream=test.flv`; the correct value is `stream=test`. With the right name there was still no media although signaling, ICE, and DTLS succeeded, which matches the old source-cleanup bug fixed in `7.0.127`/`8.0.5`. The reporter should upgrade and send logs if it persists.

## #4624 [BUG] Live source removed during publisher activation

- Issue: https://github.com/ossrs/srs/issues/4624
- Truth Record: https://github.com/ossrs/srs/issues/4624#issuecomment-5234396855
- Status: Fixed in `7.0.151+` and `8.0.5+`

`v6.0-r0` could remove a live source while its publisher was activating. Upgrade.

## #4628 [USAGE] Kick HLS viewers through the client API

- Issue: https://github.com/ossrs/srs/issues/4628
- Truth Record: https://github.com/ossrs/srs/issues/4628#issuecomment-5225936717
- Status: Closed

Find `hls-play` clients in `/api/v1/clients/` and delete them by ID.

## #4627 [USAGE] RTMP does not support PCMA audio

- Issue: https://github.com/ossrs/srs/issues/4627
- Truth Record: https://github.com/ossrs/srs/issues/4627#issuecomment-5229356765
- Status: Closed as unsupported

RTMP doesn't support PCMA. Use AAC or MP3, transcode before publishing, or use G.711 over WHIP/WHEP.

## #4625 [BUG] MP4 DVR timeline inflated by repeated DTS samples

- Issue: https://github.com/ossrs/srs/issues/4625
- Truth Record: https://github.com/ossrs/srs/issues/4625#issuecomment-5233336576
- Status: Fixed on `forge` (`2f1f67a81`), not merged or released

Same-DTS video messages (a picture plus SEI/AUD) have a real zero delta, but `SrsMp4SampleManager::write_track` treated `sample_delta_ == 0` as an empty `stts` entry and overwrote it, so a 30s DVR MP4 played for 140s. It now checks `sample_count_ == 0` (`ReproduceIssue4625.PreserveZeroDtsInMp4Stts`).

## #4623 [USAGE] Private origin address registered across unrelated networks

- Issue: https://github.com/ossrs/srs/issues/4623
- Truth Record: https://github.com/ossrs/srs/issues/4623#issuecomment-5234472135
- Status: Closed

The origin registered a private IP the proxy couldn't reach; set `SRS_DEVICE_IP` to an address the proxy can reach.

## #4620 [FEATURE] Generated names for SRT publishers without stream IDs

- Issue: https://github.com/ossrs/srs/issues/4620
- Truth Record: https://github.com/ossrs/srs/issues/4620#issuecomment-5241943782
- Status: Closed as not planned

Without a stream ID, SRT publishers use `default_streamid`. Random names would be unknown to players and change on reconnect; use stable, distinct stream IDs.

## #4617 [USAGE] Enable RTC-to-RTMP conversion

- Issue: https://github.com/ossrs/srs/issues/4617
- Truth Record: https://github.com/ossrs/srs/issues/4617#issuecomment-5242032033
- Status: Closed

RTC-to-RTMP is off by default. Use `conf/rtc2rtmp.conf` or enable `rtc_to_rtmp`, with the same vhost, app, and stream on both sides.

## #4616 [USAGE] Incorrect GB28181 Docker configuration

- Issue: https://github.com/ossrs/srs/issues/4616
- Truth Record: https://github.com/ossrs/srs/issues/4616#issuecomment-5247039175
- Status: Closed

The config exposed GB28181 port 9000 as UDP instead of TCP, hard-coded an unreachable candidate, and missed a vhost brace. Current SRS also needs an external SIP server.

## #4609 [BUG] Graceful disconnects inflated client error metrics

- Issue: https://github.com/ossrs/srs/issues/4609
- Truth Record: https://github.com/ossrs/srs/issues/4609#issuecomment-5247423361
- Status: Fixed on `winlin/develop` (`8.0.14`), not merged or released

`SrsStatistic::on_disconnect()` counted graceful disconnects (socket read/write EOF, SRT IO, HTTP stream EOF) in `srs_clients_errs_total`; they are now excluded. The reported `srs_clients` historical-max behavior was not reproduced.

## #4611 [BUG] HTTP-FLV on-demand playback regression

- Issue: https://github.com/ossrs/srs/issues/4611
- Truth Record: https://github.com/ossrs/srs/issues/4611#issuecomment-5247064760
- Status: Closed; fixed in `7.0.150` and `8.0.2` by PR #4678

## #4622 [BUG] RTMP callback parameters duplicated when tcUrl is parsed twice

- Issue: https://github.com/ossrs/srs/issues/4622
- Truth Record: https://github.com/ossrs/srs/issues/4622#issuecomment-5240365106
- Status: Fixed by `778623fa6` (`8.0.12`); merge and release pending

When `tcUrl` had a query, parsing the request a second time appended it again (`?token=abc?token=abc`) in callbacks. `srs_net_url_parse_tcurl()` now rebuilds the URL idempotently.

## #4621 [BUG] HTTP-FLV discarded the forwarded client IP

- Issue: https://github.com/ossrs/srs/issues/4621
- Truth Record: https://github.com/ossrs/srs/issues/4621#issuecomment-5241813445
- Status: Fixed and merged in `8.0.14` (PR #4711)

HTTP-FLV overwrote the parsed forwarded address with the proxy's peer IP. It now uses `X-Forwarded-For`, then `X-Real-IP`, then the peer address, for both stats and security. Only trusted proxies should send these headers; there is no allowlist.

## #4532 [FEATURE] SCTE-35 forwarding in SRT-to-RTMP

- Issue: https://github.com/ossrs/srs/issues/4532
- Truth Record: https://github.com/ossrs/srs/issues/4532#issuecomment-5247905549
- Status: Closed as not planned

SRS won't carry SCTE-35 over RTMP. The sample also used AAC LATM, which SRS doesn't support (ADTS only). Keep SCTE-35 in TS/SRT or handle it externally, and convert LATM to ADTS for RTMP.

## #4410 [LIMITATION] HEVC snapshot requires FFmpeg 8

- Issue: https://github.com/ossrs/srs/issues/4410
- Truth Record: https://github.com/ossrs/srs/issues/4410#issuecomment-5261035872
- Status: Closed as completed

FFmpeg 5.0.2 in SRS 6.0.166 couldn't read the Enhanced FLV HEVC stream, and snapshots should use `iformat flv`, not `hevc`. The Dev Docker ubuntu20 toolchain now ships FFmpeg 8.1.2, so newly built images are fixed; old images such as `6.0.166` are not.
