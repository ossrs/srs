| `h264-high-aac.ts` | H.264 High with B-frames | AAC 44.1 kHz stereo | SRT, in MPEG-TS; its unit test is in `internal/ts` |
# srs-integ

A black-box integration suite for the SRS media server. It tests features and
interfaces from outside the process, only through what a client, an operator or
a callback server can see, so a rewritten server could pass the same cases.

It is its own Go module, `srs-integ`, separate from the root `srsx` module. It
imports no package of the repository, only its own packages and third-party
modules, and needs no cgo.

## Run

From the repository root:

```bash
make integ          # The test media, then every case: the timer cases and the others, as two go test processes at once.
make integ-prepare  # Only the test media.
make bin/srs-integ  # The CLI, also built by make build.
```

One feature or one case:

```bash
go -C cmd/integ/cases test -count=1 -run 'Test(Timer_)?Hls_'
go -C cmd/integ/cases test -count=1 -run 'TestRtmp_Common/PlayBeforePublish'
```

The cases run `trunk/objs/srs`, built with every feature the suite needs, and
fail with "rebuild it ..." when `/api/v1/features` lacks one:

```bash
cd trunk && ./configure --srt=on --gb28181=on --rtsp=on --apm=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on && make
```

Flags, after `-args`:

- `-integ.srs <path>`: another SRS binary.
- `-integ.timer`: allocate ports from the timer process's part of the range; `make integ` sets it for the timer process.
- `-integ.known-bugs fail`: known-bug cases fail like normal cases; the default, `pass`, lets a known bug that fails pass.
- `-integ.scale N`: run each case N times as separate cases, named `<Case>`, `<Case>-2` to `<Case>-N`, each with its own stream on its group's server; timer cases run once, since each starts a server of its own. The scale check.
- `-integ.shards K`: split Common into K identical servers; a case gets the one its name picks. The default is 3: every case starts at once, and one single-threaded Common made the slowest real-time cases miss their 500ms limit while it was saturated in the first part of a run; 2 held until the HLS cases added their bursts.

`make integ`, `make integ-main` and `make integ-timer` pass `INTEG_ARGS` after
`-args`. The scale check runs about 1,000 cases:

```bash
make integ-main INTEG_ARGS="-integ.scale=56"
```

Each `go test` process ends with its summary: the cases passed, failed and
skipped with their reasons, the known bugs, and the 10 slowest cases. Then the
SRS processes it started and the most that ran at once, the ports it took, and
the CPU each Common server used, as a share of the one core SRS runs on. Run it in
`cmd/integ/cases` with no package argument, as above, or `go test` hides the
summary of a passing run.

Each run works in `/tmp/srs-integ/<pid>/`, one directory per server with its
env, pid file and console log. A group's directory is removed when it passes
and kept when it fails; a failing case shows the last 30 lines of the log. The
run removes its directory when every test passed, and the directories of
earlier runs whose process is gone.

The CLI gives the stream tools by hand:

```bash
bin/srs-integ probe <url>            # Print the stream report of rtmp://, rtmps://, http://...{flv,ts,aac,mp3,m3u8,mpd}, a WHEP URL (http://host:1985/rtc/v1/whep/?app=live&stream=s, offering every codec), srt://host:port?streamid=#!::r=live/s,m=request (also passphrase, pbkeylen and latency in ms) or an FLV, TS, AAC, MP3 or MP4 file; -duration, -timeout.
bin/srs-integ publish <file> <url>   # Publish an FLV file in real time, to rtmp:// or rtmps://, or an MP4 file with H.264, HEVC, VP9 or AV1 and Opus by WHIP (http://host:1985/rtc/v1/whip/?app=live&stream=s; it adds vcodec for a codec other than H.264), or an MPEG-TS file to srt://host:port?streamid=#!::r=live/s,m=publish, timed by its PCR. Neither verifies an rtmps:// certificate, like ffmpeg.
bin/srs-integ prepare -h             # Make the test media; its flags.
```

## Layout

- `main.go`: the CLI.
- `internal/server/`: the adapter, the only package that knows the SRS binary, its env var names, its ports and its paths.
- `internal/media/`: the test media: its recipes, the prepare cache, and FLV and MP4 readers, with the protection (`sinf`) of encrypted MP4 tracks.
- `internal/rtmp/`: the RTMP client: the simple and complex handshakes, chunk sizes, connect options, FMLE and Flash publish, play, FLV tags in and out including enhanced RTMP, real-time pacing, and RTMPS (`Options.TLS`, `Client.TLS`).
- `internal/report/`: the stream report every analyzer fills, and `Until`, the stop condition a case states.
- `internal/httpplay/`: the HTTP stream player the FLV, TS and audio players share: it sends its own method, Host and headers, and waits for the answer and each unit its decoder reads with a deadline.
- `internal/flv/`: the FLV analyzer, and the RTMP and HTTP-FLV players that feed it; `HTTPPlayer` also waits for the FLV header flags.
- `internal/ts/`: the TS demuxer (PAT, PMT, PES; H.264, HEVC, AAC, MP3 and Opus; each PES with the codecs of its PMT), its analyzer, and the HTTP-TS players.
- `internal/audio/`: the ADTS and MP3 frame readers, the raw .aac and .mp3 analyzer, and its HTTP players.
- `internal/mp4/`: the fragmented MP4 reader (init, moof and mdat) and the analyzer of MP4 samples, for HLS fmp4, DASH and MP4 files, progressive or fragmented.
- `internal/hls/`: the m3u8 parser (master and media playlists, keys, init maps) and the HLS player: TS or fmp4 segments, AES-128 decryption (`hls.Decrypt` for one segment), live playlists.
- `internal/dash/`: the MPD parser (segment templates and timelines) and the DASH player, which merges the video and audio representations in time order.
- `internal/rtc/`: the WebRTC client, built on pion: a WHIP publisher of samples read from an MP4 file (H.264, HEVC, VP9 or AV1, and Opus) or a raw G.711 file, and a WHEP player, or both by SRS's legacy JSON API; each session offers H.264 and Opus or the codecs of its options, and a publisher's track sends the codec SRS chose; ICE over UDP on the loopback interface, checked every 20ms rather than pion's 200ms, or with `Options.TCP` only over TCP, from pion's active TCP candidates to the server's passive one (RFC 6544); `Options.Setup` posts an offer with the DTLS setup active or passive in place of actpass; `Transport` tells whether ICE selected a UDP or a TCP pair; `Delete` of the session's Location; the publisher counts the PLIs SRS sends, and the player sends one with `RequestKeyframe`; the analyzer rebuilds H.264 and HEVC frames from RTP for the TS analyzer, VP9 frames (keyframe, profile and size from the uncompressed header) and AV1 temporal units (the sequence header's profile and size, the frame header's type), and counts each Opus or G.711 packet. `Offer` makes an offer of chosen codecs and `Post` sends it without connecting, to check an answer or an error status. A raw publisher and player, without a peer connection, for `encrypt=false` and `dtls=false`: one STUN binding to the answer's UDP candidate, or its TCP one with each packet framed as RFC 4571 says, the DTLS handshake with pion/dtls or none, then RTP and RTCP in clear; `Abort` closes its socket without a DTLS alert. `Bind` sends the binding requests of a session's offer and answer from a new UDP or TCP socket, as an ICE-controlling or controlled agent, and returns the address the server saw, `ErrClosed` when it closes the connection, or the deadline. Each session has a `Recorder`, the interceptor under pion's, which logs the RTP and RTCP as the network carries them: for each stream by SSRC, the sequence numbers a later packet skipped (`Missed`), those that came after (`Late`), those that came or were sent again (`Repeated`), and an RTX stream's original sequence numbers (`OSNs`); the RTCP packets each way, read with `Nacked` and `TWCCFeedback`; and the transport-wide sequence numbers sent. pion's NACK generator asks every 20ms rather than 100ms, at most 10 times for a packet. `Options.RTX` offers RTX (RFC 4588) for each video codec, payload type plus one, and `Options.TWCC` makes a publisher send the transport-wide sequence number. `Username` is the session's name in SRS, the server's ICE ufrag and the client's.
- `internal/srt/`: the SRT client, a caller built on the pure-Go gosrt (checked against libsrt by its unit tests, with ffmpeg as the listener both ways): `Dial` with the streamid, passphrase, key length and latency of `Options` (`ParseURL` reads them from an srt:// URL); a refused handshake is a `RejectError` with libsrt's reason, such as 1010 for a wrong passphrase and 1011 for a missing or unexpected one; `WaitClosed` reads until the server closes, and `WaitClosedProbing`, for a caller dialed with a short `PeerIdleTimeout`, also sends a null TS packet now and then, so a server that closed without the client hearing it (gosrt drops a shutdown that comes while its `Dial` returns) ends the connection by its silence; `TSBPD` is the delay each end holds what it receives, from the handshake; `Analyze` and `Play` feed the TS a player gets to the TS analyzer. `Sender` sends a TS file in messages of 7 packets, each timed by its latest PCR: `SendThrough` sends the start at once, `Stream` the rest in real time.
- `internal/rtsp/`, `internal/gb/`: the clients and analyzers.
- `internal/mock/`: the HTTP server SRS calls back, for the hooks, the heartbeat and the forward backend: it records every request (method, path, query, headers, body and its JSON) and answers by script (ok, a reject code, an HTTP error, a delay, no answer, or any JSON), routed by action and stream; `Wait` blocks until a request that matches arrives.
- `internal/check/`: the case helper.
- `cases/`: the cases.
- `testdata/`: the self-signed TLS key and certificate of the TLS listeners, for localhost and 127.0.0.1, test-only; the generated media goes in `testdata/media/`, which git ignores.

Each internal package has its own unit tests.

## Rules

### Black box

A case observes SRS only through the network (RTMP, HTTP, WebRTC, SRT, RTSP,
UDP), the HTTP API and the callbacks it sends, the files it writes, its exit
code and the signals the case sends it. No SRS source, internals or log text
decides pass or fail; the log is attached only to explain a failure.

### Env only

SRS starts as `objs/srs -e` with `SRS_*` env vars, never with a config file. A
feature that needs a file, such as several vhosts, gets no case.

### Naming

- **Normal cases:** `Test<Feature>_<Group>/<Case>`, such as `TestRtmp_Common/PlayBeforePublish`.
- **Timer cases:** `TestTimer_<Feature>_<Group>/<Case>`, such as `TestTimer_Hls_Dispose/FilesRemoved`.
- **Group:** `Common`, the one shared server with every feature on, or the name of a variant server.
- **Feature:** one name per feature area, such as `Rtmp`, `HttpFlv`, `Hls`, `Rtc`, `Api` or `Hooks`. Reuse a name from the catalog before adding one.

`make integ` runs `^TestTimer_` in one process and everything else in another,
with `-parallel 64`, at the same time.

### Servers

- **Common:** one SRS with every feature that doesn't change how other streams behave, started by the first case that uses it and stopped after every test ends. Each case uses its own stream name. It runs as 3 identical shards by default (`-integ.shards`), and a case's name picks its shard, so every `server.Common(c)` of a case is the same server.
  Its listeners: RTMP, RTMPS, the HTTP API and server, HTTPS for both, WebRTC over UDP and TCP, SRT, RTSP, the exporter, and the GB28181 media port. The MPEG-TS over UDP caster needs a variant, because a server has one stream caster. RTMP also listens on `[::]` on the same port (`Config.IPv6`), when the host has IPv6.
  Its TLS listeners use the self-signed test certificate in `testdata/` for localhost, 127.0.0.1 and ::1; `server.ClientTLS()` is a client config that trusts it.
  It plays with the lowest latency (`min_latency on`, `mw_latency 0`), so a player gets each message at once rather than in 350ms batches; a case that tests the default batching needs a variant.
  It serves every stream as HTTP-FLV at `/[app]/[stream].flv`, and as HLS (TS) at `/[app]/[stream].m3u8` and DASH at `/[app]/[stream].mpd`, both with 10s segments.
  WebRTC is on for the vhost, over UDP and TCP (`protocol all`), with its candidate at 127.0.0.1, so a session doesn't depend on the host's network interfaces; WHIP and WHEP are on the HTTP API port. The host of a WHIP or WHEP request is a candidate too (`api_as_candidates`, on by default), a name as it is, never resolved (`keep_api_domain on`, `resolve_api_domain off`), so no case depends on DNS. It doesn't convert between RTMP and WebRTC (`rtmp_to_rtc` and `rtc_to_rtmp` are off), which would transcode the audio of every stream on the shard's one thread.
  SRT is on for the vhost, with its streams converted to RTMP (`srt_to_rtmp`), and with no receive latency (`recvlatency 0`, which sets it, since SRS applies `latency` first and the `recvlatency` default of 120ms overrides it), so SRS passes on what an SRT publisher sends at once.
  Every hook (`on_connect`, `on_close`, `on_publish`, `on_unpublish`, `on_play`, `on_stop`, `on_dvr`, `on_hls`, `on_hls_notify`) calls one mock at `/hooks/<action>`, `server.Common(c).Hooks`. It accepts every request unless a case scripts a reply, and a case scripts only its own stream, such as `Hooks.Script(mock.Match{Action: "on_publish", Stream: stream}, mock.Reject(403))`. A variant gets its own mock with `Config.Hooks`.
- **Remux variants:** a vhost has one HTTP remux mount, so HTTP-TS, MP3 and AAC each have a variant, `TsMount`, `Mp3Mount` and `AacMount` (`server.RemuxConfig`), with RTMP, the HTTP API and server, and Common's latency. `server.RemuxVariant` sets the remux settings Common keeps at their defaults (`guess_has_av`, `has_audio`, `has_video`, `drop_if_not_match`, `fast_cache`), the HTTP server's `crossdomain`, or turns the remux off: `NoGuess`, `NoAudio`, `NoVideoKeep` and `RemuxOff` for HTTP-FLV.
- **HTTP-FLV players on Common:** SRS tears down the HTTP mount of every unpublished stream on one shared worker, which polls every 100ms while the stream still has a player, and refuses to publish a stream whose old mount it is tearing down. So a player left at the unpublish can make another case's republish fail. A player from `httpFlvPlayer` waits on close, out of the case time, until SRS has dropped it, and a case that unpublishes with a player attached runs on a variant.
- **Play variants:** `server.PlayConfig` sets the play settings Common keeps at their defaults (`gop_cache`, `gop_cache_max_frames`, `queue_length`, `reduce_sequence_header`, `time_jitter`, `atc`, `atc_auto`, `mix_correct`, `send_min_interval`), or replaces its latency with `min_latency off` and merged writes and reads (`mw_latency`, `mw_msgs`, `mr`), with RTMP and the HTTP API: `GopCacheOff`, `GopCacheMaxFrames`, `ReduceSequenceHeader`, `JitterZero`, `JitterOffAtcAuto`, `AtcMixCorrect`, `MergedWrite`, `SendMinInterval`, and the timer groups `QueueDefault` and `QueueLength`. Settings that don't interfere share one variant, such as `atc` with `mix_correct`.
- **SRT players:** SRS keeps no GOP for SRT either, so a case connects its SRT player before the stream is published and waits for its `on_play` (`srtPlayer`); SRS makes the player's consumer right after the hook answers, before the publisher's own hooks. A caller SRS closes right after the handshake, such as one with a bad streamid or a second publisher, is dialed with a 250ms idle timeout and checked with `WaitClosedProbing` (`dialSRTEarlyClose`, `checkSRTClosedEarly`). The publisher (`startSRTPublisher`) sends the TS test file's first 500ms of PCR at once, then real time: ffmpeg's TS muxer sends the audio of a moment hundreds of ms after its video, and the lead gets the player both sequence headers at once.
- **WebRTC players:** SRS keeps no GOP for WebRTC, and it subscribes a player after the player's DTLS handshake, on a coroutine of its own, dropping what the publisher sends before. So a case connects its players before the publisher sends, and `sendRTC` has each player ask for a keyframe (a PLI) and waits until SRS forwards the request to the publisher, which shows the player is subscribed; then it sends the file from its first keyframe, the first 200ms at once.
- **Publish variants:** `server.PublishConfig` sets the publish timeouts Common keeps at their defaults (`firstpkt_timeout`, `normal_timeout`, `kickoff_for_idle`), with RTMP, the HTTP API and Common's latency: the timer group `PublishTimeout`. SRS checks a publisher once per timeout, so `kickoff_for_idle` acts only on those checks and shares the variant with the short timeouts.
- **HLS variants:** `server.HLSConfig` sets the HLS settings Common keeps at their defaults (`hls_fragment`, `hls_window`, `hls_td_ratio`, `hls_aof_ratio`, `hls_path` with the HTTP server's dir, `hls_m3u8_file`, `hls_ts_file`, `hls_entry_prefix`, `hls_ts_floor`, `hls_use_fmp4`, `hls_fmp4_file`, `hls_init_file`, `hls_keys`, `hls_fragments_per_key`, `hls_key_url`, `hls_ctx`, `hls_ts_ctx`, `hls_master_m3u8_path_relative`, `hls_dispose`, `hls_cleanup`), with RTMP, the HTTP API and server, and Common's latency, and no HTTP remux: `Options`; `TsFloor`, because `hls_ts_floor` changes when segments are cut, shared with the keys and session settings; `Fmp4`, since fmp4 changes the muxer; and `Fmp4Keys`, fmp4 with keys and `hls_ctx` off. `server.WithHLS` turns HLS on in another variant's config, with its HTTP server, like `server.WithDASH`. On Common, HLS cases publish only the segments they need (`publishThrough`), since every burst shares a shard's one thread with the real-time cases.
- **Variant:** a server only when Common can't do it: a value Common can't have, global state, a process change, or a topology. A variant group is one top-level test that starts its SRS, runs its cases as parallel subtests, and stops the SRS.
- **DASH variants:** `server.WithDASH` turns DASH on in another variant's config, with the DASH settings Common keeps at their defaults (`dash_fragment`, `dash_window_size`, `dash_path`, `dash_mpd_file`, `dash_dispose`, `dash_cleanup`), so DASH groups run on the shared servers `RemuxOff`, `Fmp4Keys`, `SegmentsDispose` and `SegmentsKeep`, whose groups publish little, rather than on servers of their own. `dash_path` doesn't change the HTTP server's dir, so a `dash_path` under it is served by its subdir (`./objs/nginx/html/dash` as `/dash`). DASH segments are fmp4 files in a dir per stream beside the MPD, so they don't meet the HLS files.
- **Shared variants:** when one config serves the groups of several features, they share one server with `server.Shared(t, name, cfg)`: the first group that asks starts it, every other must ask with the same config, and it stops after every test, like Common. Each group still has its own name. `TestHttpFlv_RemuxOff`, `TestHls_Fmp4Default`, `TestDash_Options` and `TestSrt_Latencies` share `RemuxOff`: the HTTP remux off, HLS in fmp4 at SRS's defaults, the DASH settings of `TestDash_Options`, and SRT with `recvlatency 250` and `peerlatency 150`. `TestHls_TsFloor`, `TestHls_Keys`, `TestHls_Sessions`, `TestRtc_NackTwccOff` and `TestSrt_Options` share `TsFloor`: `hls_ts_floor`, AES-128 keys with `hls_key_url` on the CDN host, `hls_ts_ctx off`, `hls_master_m3u8_path_relative on`, hooks, WebRTC over UDP with `nack off` and `twcc off`, and SRT (`server.WithSRT`) for `TestSrt_Options`, with `default_mode publish`, `default_app srtapp`, `default_streamid #!::r=srtapp/default,m=request` and a passphrase. `TestHls_Fmp4Keys`, `TestHls_CtxOff`, `TestDash_StreamDir`, `TestRtc_Transport`, `TestRtc_PreferRtx` and `TestSrt_LatencyOnly` share `Fmp4Keys`: fmp4 with keys at the default `hls_key_url`, `hls_ctx off`, hooks, DASH with 2s fragments and the MPD in a dir per stream, and WebRTC over UDP only with `dtls_role active`, `stun_strict_check on`, `api_as_candidates off` and `nack_prefer_rtx on` (`server.WithRTC`), and SRT with `latency 300` alone. In the timer process, `TestTimer_Rtmp_QueueDefault`, `TestTimer_Hls_Dispose` and `TestTimer_Dash_Dispose` share `SegmentsDispose`: the default play queue, and HLS and DASH with 1s fragments and `hls_dispose` and `dash_dispose` at 1s, the smallest whole second. `TestTimer_Rtmp_QueueLength`, `TestTimer_Hls_Cleanup`, `TestTimer_Dash_Cleanup`, `TestTimer_Hls_Sessions` and `TestTimer_Srt_PeerIdle` share `SegmentsKeep`: a 1s play queue, HLS with 1s fragments, `hls_window 0.5` (sessions end after 1s without a request) and `hls_cleanup off`, DASH with 1s fragments and `dash_cleanup off`, and hooks, and SRT with `peer_idle_timeout 1000`.
- **Bursts:** SRS runs on one thread and shares it fairly, so many cases that send a burst at once, such as a whole file without pacing, make each of them, and the real-time cases beside them, as slow as all of them together. A case holds one of a server's 2 burst slots while it sends one (`srs.Burst(c)`); the wait for a slot is not part of its time. `publishAll` does it for the segment groups.
- **Hook mock:** it closes each connection after its reply, so SRS, which opens one per hook, leaves no socket in TIME_WAIT on an ephemeral port; a run of 1,000 cases would otherwise use them up.

### Classes

Each case declares its class first, through the case helper in
`internal/check`, and runs as a parallel subtest of its group:

```go
check.Case(t, "PlayBeforePublish", check.Normal, func(c *check.C) {
	srs := server.Common(c)
	c.Must(err, "connect")              // Fails and stops the case.
	c.CheckEqual(got, want, "frames")   // Fails and goes on.
})
```

A case checks only through `c` (`c.Check...`, `c.Must`, `c.Errorf`,
`c.Fatalf`), never through the group's `t`, so the helper decides the result.
A case over its limit fails with `too slow: <ms> > <limit>`. Time is measured
inside the case, without a server start (`server.Common` and `server.Group`)
and without its cleanups.

| Class | Limit | Cases |
|---|---|---|
| Fast | ≤100ms | API, hooks, security, static files, a play that gets its first frames from the GOP cache |
| Normal | ≤500ms | Most streaming cases, WebRTC and SRT handshakes, segment groups, edge and forward chains |
| Timer | ≤10s, only in `TestTimer_*` groups | Cases that wait on a whole-second SRS timer or a 1s tick, with every timeout at its smallest value |

### Real time

Streams are published in real time, like ffmpeg `-re`. Segment groups (HLS,
DASH and DVR) publish without `-re`, because segments follow media timestamps,
not wall time. Their players fetch whole segments, so a report can go a
little past the stop condition.

### Stream checks

A case checks a stream with the suite's own analyzers, never ffprobe. They
fill one stream report (`internal/report`): codecs, picture size, sample
rate, sequence headers, keyframes and frames, timestamps that only go forward
without gaps, the frames out of time order across the two tracks
(`Unordered`), and the A/V sync difference. TS and raw AAC carry the sequence
headers in band, before each keyframe or in each frame, so their report counts
one each time they first appear or change. The case states when to stop, as a
`report.Until` such as "both sequence headers, 1 keyframe, 5 video frames", so
the analyzer stops as soon as the check can pass:

```go
r, err := flv.PlayHTTP(ctx, nil, url, report.Until{VideoHeader: true, AudioHeader: true, Keyframes: 1, VideoFrames: 5})
c.Must(err, "play") // On a timeout, the error has what it waited for and what it got.
c.CheckNoError(r.CheckTimestamps(100*time.Millisecond), "timestamps")
```

### Test media

The media is made from one source, `doc/source.flv`, never committed.
`make integ-prepare`, which `make integ` runs first, transcodes a 30s cut of
it into `testdata/media/` with each recipe of `internal/media`, every video
with a fixed 1s GOP at 25 fps:

| File | Video | Audio | For |
|---|---|---|---|
| `h264-high-aac.flv` | H.264 High with B-frames | AAC 44.1 kHz stereo | RTMP, HTTP, HLS, DASH and SRT, like a real encoder |
| `h264-baseline-aac.flv` | H.264 Baseline, no B-frames | AAC | RTMP to WebRTC and RTSP |
| `h264-baseline-opus.mp4` | H.264 Baseline | Opus 48 kHz stereo | WHIP |
| `hevc-aac.flv` | HEVC Main | AAC | enhanced RTMP |
| `hevc-opus.mp4` | HEVC Main, no B-frames | Opus | WebRTC |
| `hevc-aac.ts` | HEVC Main with B-frames | AAC 44.1 kHz stereo | SRT, in MPEG-TS; its unit test is in `internal/ts` |
| `h264-opus.ts` | H.264 High with B-frames | Opus 48 kHz stereo | SRT, in MPEG-TS; its unit test is in `internal/ts` |
| `aac-only.flv` | none | AAC | audio only |
| `h264-only.flv` | H.264 High | none | video only |
| `h264-high-mp3.flv` | H.264 High | MP3 44.1 kHz stereo | MP3 |
| `vp9-opus.mp4` | VP9 profile 0 | Opus | WebRTC |
| `av1-opus.mp4` | AV1 Main | Opus | WebRTC |
| `pcmu.ul`, `pcma.al` | none | raw G.711 mu-law or A-law, 8 kHz mono | WebRTC, with the video of `h264-baseline-opus.mp4` |

- **ffmpeg:** `$FFMPEG`, then `PATH`, then `~/.local/bin`, the first with libx264, libx265 and an Opus encoder (libopus, or the native `opus`). Cases use the same lookup.
- **macOS:** when no ffmpeg is found, or it lacks libvpx-vp9 or libsvtav1, prepare runs `skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh` to build one in `~/.local/bin`. Other tests share that binary, so it first waits until the Full-tier lock is free and no ffmpeg runs.
- **Unavailable:** a recipe whose encoder the ffmpeg lacks, such as VP9 and AV1 in CI, is recorded as unavailable, and its cases skip with "ffmpeg lacks <encoder>".
- **Cache:** `testdata/media/manifest.json` records the source's size and SHA-256, each recipe and the ffmpeg version. Only a file whose entry changed is made again.
- **Missing or stale:** a case gets a file with `media.Path(c, media.H264HighAAC)`, which fails with "run make integ-prepare" when the file is missing or stale. It never skips for that.
- **Checks:** ffmpeg versions make different bytes, so nothing checks bytes. The unit tests of `internal/media`, which need `make integ-prepare` first, read each file and check its codecs, profile, B-frames, GOP, frame rate, sample rate, duration and frame counts.

### Known bugs

A case that finds an SRS bug is marked `KnownBug(c, "<reason>")`, not fixed or
skipped. It still runs: a failure passes and logs `KNOWN BUG: <reason>`, and a
pass fails with "bug fixed, remove the KnownBug mark".
`-integ.known-bugs=fail` makes them fail like normal cases.

### Ports

The suite owns ports 14000–14999: the main process allocates from 14000–14799
and the timer process from 14800–14999. `SRS_INTEG_PORTS` moves or widens the
range. A server gets a port only for each listener it turns on, and one for
its hook mock when it has one, never an SRS default port, and a port is never
handed out twice in a run. When the range runs out, the run fails with "port
range exhausted, widen SRS_INTEG_PORTS".

## Catalog

One row per group: the feature, the group, its cases, their class, and the
utest scenario it ports, if any.

| Feature | Group | Cases | Class | Ports utest scenario |
|---|---|---|---|---|
| Api | Smoke | Versions, BuildFeatures, OnlyItsListener | Fast | |
| Api | Common | Versions, ListenersAccept | Fast | |
| Dash | Common | Published over RTMP without `-re`, only the segments a case needs; `dash_fragment 10` cuts at the keyframe at 11s: PublishPlay (H.264/AAC, 15s from two segments of both representations), PublishPlayHevc (without the encoder's info frame); DefaultFiles (`[app]/[stream].mpd` as `application/dash+xml`, `[app]/[stream]/video-init.mp4` and `audio-init.mp4` as `video/mp4`, `video-<n>.m4s` and `audio-<n>.m4s` as `video/iso.segment`, the BaseURL `<stream>/`, the first video segment at 0 for 11s); NeverPublished (MPD, init and segment 404), MissingSegment (a listed segment 200, another 404) | Normal | `ai17` (DASH) |
| Dash | Options | `dash_fragment 2` (segments of 3s), `dash_window_size 3`, `dash_path ./objs/nginx/html/dash` (served as `/dash`), `dash_mpd_file [app]/[stream]-manifest.mpd`, on the shared server `RemuxOff`; each case publishes only the 3 or 5 segments it needs: MpdTags (the live profile, `dynamic`, the BaseURL, the availability and publish times, one period at 0, an audio and a video adaptation set with aligned segments that start with a SAP, the representations `audio` (`mp4a.40.2`) and `video` (`avc1`, 768x320), the init and media templates, timescale 1000); SegmentTimeline (consecutive numbers and times without a gap, video segment n at 3n s for 3s, each audio segment ending at the first frame of the next video segment); InitFiles (one track each, `avc1` 768x320 and `mp4a` 44.1 kHz stereo, no samples); SegmentsOnGop (each video segment starts with a keyframe and holds 3 GOPs, the next 3s later; audio segments are AAC); PublishPlay, PublishPlayHevc (the init has `hvc1` or `hev1`); HevcInfoFrameSkipped (known bug); HevcCodecs (known bug); PathTemplates (the files under `dash_path` where the templates put them, the default MPD 404); WindowSize (known bug); UpdatePeriod (known bug) | Normal | `ai17` (DASH) |
| Dash | StreamDir | `dash_fragment 2`, `dash_mpd_file [app]/[stream]/index.mpd`, the default window, on the shared server `Fmp4Keys`: DefaultWindow (the MPD lists the last 5 of 7 segments of each representation); InitBesideSegments (known bug) | Normal | |
| Dash | Dispose | `dash_dispose 1`, `dash_fragment 1` (segments of 2s), on the shared timer server `SegmentsDispose` (`TestTimer_Dash_Dispose`; SRS checks its sources every 3s): FilesRemoved (after the unpublish the MPD and every segment go at least 1s after the last media and by the next check, then 404); IdlePublisherDisposed (also while the publisher stays connected and sends nothing); InitFilesRemoved (known bug); Normal: CleanupRemovesExpired (with `dash_cleanup` at its default, 17s of 2s segments keep 10s: segment 0 of each representation is removed and 404) | Timer, Normal | |
| Dash | Cleanup | `dash_cleanup off`, `dash_fragment 1`, on the shared timer server `SegmentsKeep` (`TestTimer_Dash_Cleanup`): ExpiredKept (17s of 2s segments: the MPD lists the last 5, segment 0 of each representation stays on disk and answers 200) | Normal | |
| HttpAudio | AacFastCache | LateJoinGetsCache: with `fast_cache 1`, a player that joins a held 900ms stream once the cache has it gets every audio frame at once, and nothing more (`TestTimer_HttpAudio_AacFastCache`; SRS fills the cache on a coroutine that polls every 500ms) | Timer | |
| HttpAudio | AacMount | PublishPlay, PublishPlayAudioOnly: the AAC as raw ADTS, joining 200ms into the stream; Mp3StreamEnds (an MP3 stream on the AAC mount ends without a frame); NoCacheLateJoin (a player of a held stream gets nothing: no GOP cache for audio, no fast cache by default). The remux cases, with hooks: ResponseHeaders (200, `audio/x-aac`); MultiLevelApp, ParamInUrl, VhostFromHost (`on_play` app, param, vhost); NoAppNotFound, WrongExtensionNotFound (`.flv`): 404; OnPlayRejected (401); XForwardedFor (`on_play` and the API); CorsHeaders; PlayBeforePublish, RepublishPlays, ManyPlayers (8), PlayerCloseCleanup, UnpublishEndsStream (the stream ends, `on_stop`); CloseBeforePublish | Normal | `ai26` (a viewer before the publisher) |
| HttpAudio | Mp3FastCache | LateJoinGetsCache, as AacFastCache for MP3 (`TestTimer_HttpAudio_Mp3FastCache`) | Timer | |
| HttpAudio | Mp3Mount | PublishPlay: the MP3 of H.264/MP3, joining 200ms into the stream; AacStreamEnds (an AAC stream on the MP3 mount ends without a frame); NoCacheLateJoin; the remux cases of AacMount, with `audio/mpeg` | Normal | `ai26` (a viewer before the publisher) |
| HttpFlv | Common | PublishPlay, PublishPlayHevc (enhanced RTMP), joining 200ms into the stream | Normal | |
| HttpFlv | Common | Signaling: ResponseHeaders (200, `video/x-flv`, both tracks in the FLV header); MultiLevelApp (`/live/a/b/<stream>.flv`, `on_play` app `live/a/b`), ParamInUrl (the query is the `on_play` param), VhostFromHost (a Host SRS doesn't have plays on the default vhost); NoAppNotFound (`/<stream>.flv`), WrongExtensionNotFound (`.mp3` on the FLV mount): 404; OnPlayRejected (`on_play` rejects: 401); XForwardedFor, XRealIP (the client IP in `on_play` and `/api/v1/clients/{id}`); CorsHeaders (`Access-Control-Allow-Origin: *` with an Origin), CorsOptions (the preflight is 200) | Normal | |
| HttpFlv | Common | Media and the guessed FLV header: PublishPlayAudioOnly (audio only), PublishPlayVideoOnly (video only), PublishPlayMp3; GuessFromFirstMessages (a player that joins before the first audio frame gets a video-only header and no audio, as the `guess_has_av` docs say) | Normal | |
| HttpFlv | Common | Lifecycle: RepublishPlays (the publisher retries while SRS refuses it), ManyPlayers (8 at once), PlayerCloseCleanup (gone from `/api/v1/clients/{id}`, the stream counts only the publisher); bad input: MalformedRequest (an error or a close, then the server serves the next player), CloseBeforePublish (a player waiting for a stream closes: gone from the API, and the stream plays once published) | Normal | `ai26` (a viewer before the publisher) |
| HttpFlv | Common | Over the HTTPS server, with players that trust the test certificate: HttpsPublishPlay (the player verified it), HttpsPublishPlayHevc; HttpsResponseHeaders (200, `video/x-flv`, CORS); HttpsStreamNaming (multi-level app and param in `on_play`); HttpsAndHttpPlayers (2 HTTPS and 2 HTTP players of one stream); HttpsPlayerCloseCleanup | Normal | |
| HttpFlv | NoGuess | `guess_has_av off`, `fast_cache 1`: PlayBeforePublish, EarlyJoinKeepsAudio (both tracks in the header, the late audio plays), AudioOnlyHeaderHasBoth, UnpublishEndsStream (the player's stream ends, `on_stop`), RepublishAfterOtherStream (known bug), FastCacheIgnored (a late player gets the last GOP from its keyframe) | Normal | |
| HttpFlv | NoAudio | `guess_has_av off`, `has_audio off`, `crossdomain off`: AudioDropped (a video-only header, no audio), NoCorsHeaders, OptionsNotAllowed (405) | Normal | |
| HttpFlv | NoVideoKeep | `guess_has_av off`, `has_video off`, `drop_if_not_match off`: VideoKept (an audio-only header, and the video still comes) | Normal | |
| HttpFlv | RemuxOff | NotServed: a published stream is 404 over HTTP-FLV and plays over RTMP; on the shared server `RemuxOff` | Normal | |
| HttpTs | NoAudio | `guess_has_av off`, `has_audio off`: VideoOnlyProgram (the PMT has video only), AudioOnlyProgram (the PMT has the video of `has_video` too), AudioKept (`has_audio` sets only the first PMT, so the audio plays) | Normal | |
| HttpTs | NoVideo | `guess_has_av off`, `has_video off`: AudioOnlyProgram (audio only), VideoOnlyProgram (the PMT has the audio of `has_audio` too), VideoKept | Normal | |
| HttpTs | TsMount | PublishPlay, PublishPlayHevc, PublishPlayMp3 (MPEG audio in TS), PublishPlayAudioOnly, PublishPlayVideoOnly (the PMT has only the track the stream has), joining 200ms into the stream; EarlyJoinKeepsAudio (a player that joins before the first audio frame gets a video-only PMT, then a new PMT with the audio, which plays, unlike HTTP-FLV's guessed header); LateJoinFromKeyframe (a late player gets the GOP cache from its keyframe); the remux cases of HttpAudio AacMount, with `video/MP2T`; over the HTTPS server, the Https cases of HttpFlv Common with `video/MP2T`, and HttpsUnpublishEndsStream | Normal | `ai26` (a viewer before the publisher) |
| Https | Common | The HTTPS server's TLS: ServesTestCertificate (one verified chain of the test certificate, TLS 1.2 or later, an HTTP answer over it); PlainHttpOnHttpsPort, BadTlsRecord (closed, at most after a TLS alert); HttpsOnHttpPort (the handshake fails at once); CloseInTlsHandshake. Each checks the server serves the next client. The streams over HTTPS are the Https cases of HttpFlv Common and HttpTs TsMount | Normal | |
| Hls | Common | Published over RTMP without `-re`, only the 10s segments a case needs, played through the `hls_ctx` master playlist: PublishPlay (H.264/AAC, 15s from two segments), PublishPlayHevc, PublishPlayMp3 (no audio sequence header), PublishPlayVideoOnly, PublishPlayAudioOnly (a segment of `hls_aof_ratio` 2.1 by 10s, plus at most a 720ms batch of audio), HttpsPublishPlay (over the HTTPS server); PlaylistTags (the master playlist with its `hls_ctx`, `application/vnd.apple.mpegurl`; version 3, target duration 10, media sequence 0, `<stream>-<seq>.ts` with the session's `hls_ctx`, 10s segments, a discontinuity at the first, no `#EXT-X-ENDLIST` while live); SegmentsOnGop (each segment is `video/MP2T`, starts with a keyframe, holds 10 GOPs of 1s, and the next starts 10s later); bad input: NeverPublished (playlist and segment 404), MissingPlaylist (the stream under another app: 404), MissingSegment (a listed segment 200, another 404) | Normal | `ai13` (HLS) |
| Hls | Options | `hls_fragment 2`, `hls_td_ratio 1.5`, `hls_aof_ratio 2.5`, `hls_window 7`, `hls_path ./hls` (and the HTTP server's dir), `hls_m3u8_file [app]/[stream]/index.m3u8`, `hls_ts_file [app]/[stream]/seg-[seq].ts`, `hls_entry_prefix http://cdn.example.com`: WindowAndSequence (3s segments, target duration 3, the window keeps the fewest that pass 7s, media sequence 6); ExpiredSegmentsRemoved (`hls_cleanup`: files and HTTP 404 for the segments out of the window); PathTemplates (the files where the templates put them, the default path 404); EntryPrefix (each URI is the prefix, the playlist's dir and the segment, and a player through the CDN plays them); AudioOnlyAofRatio (5s segments plus at most a batch, target duration 6); RepublishDiscontinuity (a republish starts a segment with a discontinuity and the sequence goes on; on this variant, which has no HTTP remux, so the republish doesn't wait for Common's HTTP-FLV teardown) | Normal | `ai13` (HLS) |
| Hls | Fmp4 | `hls_use_fmp4 on`, `hls_fragment 2`, `hls_td_ratio 1.5`, `hls_aof_ratio 2.5`, `hls_window 7`, `hls_path ./hls`, `hls_fmp4_file [app]/[stream]-seg-[seq].m4s`, `hls_init_file [app]/[stream]-init.mp4`, `hls_entry_prefix http://cdn.example.com`, HTTPS; SRS cuts an fmp4 segment at the first keyframe where it already lasts the 3s target to its last frame, so segments of 4s: PublishPlay (H.264/AAC), PublishPlayHevc, PublishPlayMp3 (known bug), PublishPlayVideoOnly, PublishPlayAudioOnly (5s segments plus at most a batch, target duration 6), HttpsPublishPlay; PlaylistTags (version 7, target duration 4, media sequence 0, `#EXT-X-MAP` with the init file, `<stream>-seg-<seq>.m4s` with the session's `hls_ctx`, 4s segments); InitFile (`video/mp4`, an H.264 and an AAC track, no samples); SegmentsOnGop (each segment is `video/iso.segment`, starts with a keyframe, holds 4 GOPs, the next starts 4s later); WindowAndCleanup (2 segments of 4s for the 7s window, media sequence 3, the expired files removed and 404, the init file kept); PathTemplates (the files where the templates put them, none at the default names); EntryPrefix (known bug); RepublishSequence (the sequence goes on, the segment after the republish plays); RepublishDiscontinuity (known bug); bad input: NeverPublished (playlist, segment and init file 404), MissingSegment | Normal | `ai13` (HLS) |
| Hls | Fmp4Default | `hls_use_fmp4 on` at SRS's defaults, 10s fragments (segments of 11s), on the shared server `RemuxOff`: DefaultFiles (`[app]/[stream].m3u8`, `[app]/[stream]-[seq].m4s`, `[app]/[stream]/init.mp4`, the init file served at its path); DefaultInitPlays (known bug) | Normal | |
| Hls | TsFloor | `hls_ts_floor on`, `hls_fragment 2`, `hls_ts_file [app]/[stream]-[timestamp].ts`, on the shared server `TsFloor`: FloorNames (consecutive timestamps from about the wall time over 2s), FloorSegmentsPlay (through the CDN host of the keys) | Normal | |
| Hls | Keys | `hls_keys on`, `hls_fragments_per_key 2`, `hls_key_url http://cdn.example.com/?token=abc123&sig=xyz789`, on the shared server `TsFloor`: KeyTags (`#EXT-X-KEY` AES-128 with a 16-byte IV, one key for segments 0 and 1 and another, with another IV, from 2); KeyUrlQuery (the key URI is `hls_key_url`, the key file and the query, and the key is served there); KeyFiles (a 16-byte key file under `hls_path` for segments 0 and 2, different, none for 1, each served over HTTP); SegmentsDecrypt (a served segment is not TS; decrypted with its key and IV it is TS from a keyframe; the next key does not decrypt it); PublishPlay (a player follows the keys) | Normal | `ai13` (HLS) |
| Hls | Sessions | The `hls_ctx` sessions, with `hls_master_m3u8_path_relative on` and `hls_ts_ctx off`, on the shared server `TsFloor`: MasterRelative (the master playlist links `<stream>.m3u8?hls_ctx=<session>`); NoTsCtx (the session's segments have no `hls_ctx`); QueryKept (the first request's query goes after the `hls_ctx`); ChosenCtx (a player's own `hls_ctx` names the session); ClientPerSession (each session an `hls-play` client by its `hls_ctx` in the API); OnPlayPerSession (one `on_play` per session, with the session as `client_id`); RefusedNotKept (a session refused by `on_play` answers 401, a retry with its `hls_ctx` is checked and refused again, and it is no client) | Normal | `ai13` (HLS) |
| Hls | Fmp4Keys | `hls_use_fmp4 on`, `hls_fragment 2`, `hls_init_file [app]/[stream]-init.mp4`, `hls_keys on`, `hls_fragments_per_key 2`, the default `hls_key_url`, on the shared server `Fmp4Keys`: KeyTags (`#EXT-X-KEY` SAMPLE-AES with a 16-byte IV, one key for segments 0 and 1, the key file); InitProtected (`encv` of `avc1` with the 1:9 pattern and `enca` of `mp4a`, `cbcs`, a KID and a constant IV); SamplesEncrypted (known bug); DefaultKeyUrl (known bug) | Normal | |
| Hls | CtxOff | `hls_ctx off`, on the shared server `Fmp4Keys`: NoMaster (the playlist is the media playlist, segments without `hls_ctx`); NoSession (no `on_play` and no `hls-play` client after a player fetched the playlist and a segment) | Normal | |
| Hls | Dispose | `hls_dispose 1`, `hls_fragment 1`, on the shared timer server `SegmentsDispose` (`TestTimer_Hls_Dispose`; SRS checks its sources every 3s): FilesRemoved (after the unpublish the playlist and every segment go at least 1s after the last media and by the next check, then the session's playlist and a segment are 404); IdlePublisherDisposed (also while the publisher stays connected and sends nothing); IdlePublisherResumes (known bug); StreamingNotDisposed (a real-time stream keeps its playlist past the dispose time and a check, with new segments); RepublishAfterDispose (a new playlist that plays) | Timer | |
| Hls | Cleanup | `hls_cleanup off`, `hls_fragment 1`, `hls_window 0.5`, on the shared timer server `SegmentsKeep` (`TestTimer_Hls_Cleanup`): ExpiredKept (the playlist lists only the newest of 4 segments, the 3 before it stay on disk and answer 200) | Normal | |
| Hls | Sessions | The end of an `hls_ctx` session, with `hls_window 0.5`, on the shared timer server `SegmentsKeep` (`TestTimer_Hls_Sessions`; SRS checks the sessions every 5s): IdleSessionExpires (a session with no request leaves the API at least 1s, twice `hls_window`, after its last one and by the next check, with `on_stop`); ActiveSessionKept (a session that keeps requesting outlives one started with it); EndedCtxStartsNewSession (the `hls_ctx` of an ended session starts a new session by it: the master playlist, `on_play` and a client again) | Timer | `ai13` (HLS) |
| Hooks | Common | OnPublish: the `on_publish` fields, then `on_unpublish` after the publisher closes; OnPublishRejected: a scripted reject code fails the RTMP publish | Normal | |
| Rtc | Common | WHIP publishes the H.264 baseline and Opus test file, its first 200ms at once and the rest in real time, and WHEP plays it (H.264 Baseline 768x320 with one sequence header from the first keyframe, Opus 48 kHz stereo, timestamps): WhipAnswer (201, `application/sdp`, a Location to DELETE with the app, stream, session and token; the answer receives H.264 and Opus, with a host candidate on 127.0.0.1 and SRS's UDP port and a sha-256 fingerprint); WhepAnswer (the same, sending); PublishPlay; WhipPlayPath (`/rtc/v1/whip-play/`); StreamNaming (stream `a/b/<stream>` is app `live/a/b`, in `on_publish` and the Location, and plays there); DeletePublisher (200, `on_unpublish`, the publisher and the stream leave the API, the stream can be published again); DeletePlayer (200, `on_stop`, the player leaves the API); DeleteWrongToken (refused, the stream plays on); LegacyPublishPlay (`/rtc/v1/publish/` and `/rtc/v1/play/`: code 0, the SDP and a session id); LegacyBadBody (HTTP 200 with code 400); PlayBeforePublish; ManyPlayers (4); SecondPublisherRejected (409, the first plays on); RepublishPlays (a player that stays plays the new publish from its keyframe); PublisherCloseCleanup, PlayerCloseCleanup (a peer connection closed without DELETE: the hook, and gone from the API). Errors: WhipBadSdp, WhipEmptySdp, WhepBadSdp, WhipNoBundle (an offer without BUNDLE), WhipRecvonlyOffer, WhepSendonlyOffer: 400; WhipBusyRtmp (a stream RTMP publishes: 409, no `on_publish`); WhipHookRejected, WhepHookRejected (`on_publish` or `on_play` refused: 401); WhipRejectedThenPublish (known bug); WhepMissingStream (201, and nothing comes); WhepRtmpStream (a stream only RTMP publishes, with `rtmp_to_rtc` off: refused). Parameters, most by posting an offer and reading the answer: WhipVcodec (`vcodec=vp9` of an H.264 and VP9 offer: VP9 only); WhipCodecAlias (`codec` when `vcodec` is not set); WhipVideoDefault (H.264 over an earlier VP9); WhipVcodecNotOffered (400); WhipAcodec (`acodec` PCMU or PCMA in any case, Opus by default); WhipAcodecNotOffered (400); WhepVcodecPlays (`vcodec=h264` plays); WhepVcodecMismatch (`vcodec=hevc` of an H.264 stream: 400); WhepAcodecIgnored (a player gets the stream's Opus); EipCandidate (a host candidate at the `eip` address beside 127.0.0.1, and the session plays); CandidateParam (`candidate`, the other name); EipOverCandidate; DtlsOffAloneEncrypts (`dtls=false` alone keeps DTLS and SRTP, a pion player plays); SrtpByDefault (a raw client that reads RTP in clear gets packets that don't play); EncryptOffPlay, EncryptOffPublish (`encrypt=false`: DTLS, then clear RTP, played and published by a raw client); PlaintextPlay, PlaintextPublish (`dtls=false&encrypt=false`: no DTLS either). Codecs, SRS never transcoding between WebRTC sessions: PublishPlayHevc, PublishPlayVp9, PublishPlayAv1 (WHIP with `vcodec`, Opus), PublishPlayPcmu, PublishPlayPcma (H.264 with G.711 8 kHz mono): the publisher and a player that offers every codec are answered with the stream's codecs only, and the player plays them (codec, profile, 768x320, one sequence header, timestamps); HevcToAvcOnlyPlayer, Vp9ToAvcOnlyPlayer, Av1ToAvcOnlyPlayer (a player that offers only H.264, or asks `vcodec=h264`: 400, while the stream's codec is answered); AvcToPlayerWithoutAvc; PcmuToOpusOnlyPlayer, PcmaToOpusOnlyPlayer, OpusToG711OnlyPlayer (400, the stream's audio codec answered); PlayBeforePublishHevc, PlayBeforePublishAv1 (a player with `vcodec` before the publisher gets the stream); PlayBeforePublishDefault (no `vcodec` before the publisher: H.264 and Opus); PlayBeforePublishVp9 (refused: SRS has H.264, HEVC and AV1 tracks only before a publish). Transport, with ICE over UDP and over TCP (`protocol all`): AnswerCandidates (127.0.0.1 on the UDP port, and on the TCP port as a passive TCP candidate); TcpPublishPlay, TcpPublishUdpPlay, UdpPublishTcpPlay (each session selects the pair of its transport, and the player plays); TcpSecondConnectionRejected (a second TCP connection for a connected session: closed without a STUN answer, the session plays on); TcpBinding (SRS answers with the client's address; a connection closed before DTLS frees the session for another); TcpUnknownSession, TcpFirstPacketNotStun, TcpOversizePacket (a frame over 1,500 bytes): closed, and a new session binds; TcpDisconnectEndsSession (a TCP connection closed after DTLS without an alert: `on_unpublish` at once, the publisher leaves the API, the stream can be published again); UdpBinding (the client's address); UdpBindingWrongPassword, UdpUnknownSession (no answer); ControlledAccepted (an ICE-controlled agent, with `stun_strict_check` off); DtlsAnswerPassive (an actpass offer gets `dtls_role`, passive by default); DtlsOfferActive, DtlsOfferPassive (SRS answers the other role, and the stream plays); ApiAsCandidate (a request to 192.0.2.10 makes it a UDP and a TCP candidate); ApiDomainKept (a host name is a candidate as it is); EipWithPort (`eip=<ip>:<port>` sets the port of both its candidates); IceCredentials (`ice-ufrag` and `ice-pwd` in the answer, and the binding is checked with that password); IceCredentialsInvalid (a ufrag under 4 or a pwd under 22 characters: refused) | Normal | `ai16` (WHIP), `ai34` (RTC codec negotiation), `ai23` (RTC over TCP) |
| Rtc | Transport | `dtls_role active`, `stun_strict_check on`, `api_as_candidates off`, WebRTC over UDP only, on the shared server `Fmp4Keys`: UdpOnly (one UDP candidate, no TCP one); ApiAsCandidatesOff (the request's address is not a candidate); DtlsRoleActive (an actpass offer: SRS answers active, the DTLS client, and the stream plays); DtlsRoleActiveOfferActive (an active offer: passive, and it plays); StrictCheckControlledRefused (no answer to an ICE-controlled agent, an answer to the controlling one) | Normal | |
| Rtc | Common | Recovery, with SRS's NACK simulator (`/rtc/v1/nack/?username=<session>&drop=<n>`, built with `--simulator=on`), which drops the next packets SRS would send a player or has received from a publisher; the cases publish only video, so NACK covers each dropped packet, and send 400ms at once after arming it. NackApiInvalidDrop (no, 0, a negative or a non-number `drop`: code 5023, the query echoed), NackApiNoSession (code 5022) are Fast. NackPlayerPlain (4 packets dropped to a player: it NACKs each, and SRS sends each again on the media stream, with `nack_prefer_rtx` off); NackPlayerRtxNotPreferred (a player that offers RTX: no rtx in the answer, the same plain retransmission); NackPublisherPlain (4 packets of a publisher dropped: SRS NACKs exactly those, the publisher sends them again, and the player gets them late); TwccFeedback (a publisher with the transport-wide sequence number: the extension in both answer sections, and SRS's feedback counts from 0 and covers sequence numbers the publisher sent) | Fast, Normal | |
| Rtc | PreferRtx | `nack_prefer_rtx on`, on the shared server `Fmp4Keys`: PlayerRtx (a player that offers RTX: rtx for H.264 and an RTX stream (FID) in the answer, and each dropped packet comes on the RTX stream with its original sequence number, none again on the media stream); PlayerWithoutRtxPlain (no rtx offered: none answered, plain retransmission); PublisherRtx (a publisher that offers RTX: rtx answered, SRS's NACKed packets sent on the publisher's RTX stream only, and the player gets them on the media stream) | Normal | |
| Rtc | NackTwccOff | `nack off` and `twcc off`, on the shared server `TsFloor`: NackOffPlayer (no NACK feedback in the answer, the player doesn't NACK, and a NACK sent anyway gets no packet: a keyframe request after it reaching the publisher shows SRS handled it); NackOffPublisher (SRS NACKs no dropped packet of the publisher in 100ms after the packets that follow them, and the player never gets them); TwccOff (no TWCC extension or feedback in the answer, so the publisher sends no transport-wide sequence number, and no feedback in 150ms) | Normal | |
| Rtmp | Common | PublishPlay, PublishPlayHevc (enhanced RTMP) | Normal | |
| Rtmp | Common | Signaling: HandshakeSimple, HandshakeComplex (the server answers in the client's kind); ChunkSize128, ChunkSize4096, ChunkSize60000 (publisher and player); ConnectResult (`NetConnection.Connect.Success`, server chunk size 60000 before it); VhostFromTcUrl, VhostFromQuery, VhostDefaultQueryDropped (an unknown vhost goes to the default one); ParamInStream, ParamInTcUrl, AppFromTcUrl, MultiLevelApp (`live/a/b/<stream>`), Ipv6 (`::1`, skipped on a host without IPv6); PublishFmle, PublishFlash, PlayStatuses, UnpublishFmle, UnpublishFlash (the status replies and `on_unpublish`); ConnectWithoutTcUrl, BadTcUrl (closed, and the server serves the next client) | Normal | `srs_utest_manual_rtmp`, `ai26` (multi-level apps) |
| Rtmp | Common | Media: PublishPlayBaseline (profile Baseline), PublishPlayAudioOnly, PublishPlayVideoOnly, PublishPlayMp3 (no audio sequence header) | Normal | |
| Rtmp | Common | Lifecycle: PlayBeforePublish (one `onMetaData`), ManyPlayers (8 at once), SecondPublisherRejected (no `NetStream.Publish.Start`, the first still plays), RepublishNewConnection, RepublishSameConnection (FCUnpublish then publish: the player stays, gets no status or media between, then the new sequence headers), PublisherCloseCleanup, PlayerCloseCleanup (the client and the stream leave `/api/v1/clients/{id}` and `/api/v1/streams/{id}`) | Normal | `ai26` (a viewer before the publisher) |
| Rtmp | Common | Play defaults: GopCacheLateJoin (a late player gets the last GOP from its keyframe, nothing before), SequenceHeaderRepeated (both kept) | Normal | |
| Rtmp | GopCacheOff | PlayGetsNoCache: a late player gets the headers, no frame, then live frames from mid-GOP | Normal | |
| Rtmp | GopCacheMaxFrames | CacheLimited (at most 10 cached messages), CacheStartsAtKeyframe (known bug) | Normal | |
| Rtmp | QueueDefault | QueueKeepsBurst: 20s at once to a paused player, all kept (`TestTimer_Rtmp_QueueDefault`, on the shared timer server `SegmentsDispose`, SRS reads an unpause only after the 500ms sleep of a paused player) | Timer | |
| Rtmp | QueueLength | QueueShrinks: with a 1s queue, a paused player loses more than 1s of a 20s burst (`TestTimer_Rtmp_QueueLength`, on the shared timer server `SegmentsKeep`) | Timer | |
| Rtmp | ReduceSequenceHeader | DuplicateDropped (each header once), ChangedKept (a different video header is kept) | Normal | |
| Rtmp | Common | Timestamps and latency at the defaults: JitterFullStartsAtZero (a stream from 100s plays from near zero, with the publisher's steps), JitterFullFixesJumps (a 10s jump forward and a 5s jump back play without a gap or a step back), InterleaveKept (audio sent 200ms late reaches the player in the order sent), MinLatencyNoHold (the headers and one keyframe come at once) | Normal | |
| Rtmp | JitterZero | StartsAtZero (every timestamp 100s earlier), KeepsJumps (a 10s jump forward is kept), BackJumpPlays (known bug) | Normal | |
| Rtmp | JitterOffAtcAuto | KeepsTimestamps (as published, from 100s), BackJumpPlays (known bug), AtcAutoBravo (metadata with `bravo_atc`: a late player gets the metadata and headers at the GOP's time), AtcAutoNoBravo (at their own time) | Normal | |
| Rtmp | AtcMixCorrect | KeepsTimestamps (each track as published), LateJoinHeadersAtGop (the metadata and headers at the GOP's time), MixCorrectOrders (audio sent 200ms late reaches the player in time order) | Normal | |
| Rtmp | MergedWrite | HoldsUntilLatency (more than 4 messages held until they span 200ms), HoldsUntilMsgs (240ms held until 5 messages), MergedReadPlays (a real-time stream plays with merged reads) | Normal | |
| Rtmp | SendMinInterval | PacesMessages (10 messages sent at once reach the player at least 20ms apart, in order) | Normal | |
| Rtmp | PublishTimeout | `firstpkt_timeout` 100ms, `normal_timeout` 200ms, `kickoff_for_idle` 0.3s (`TestTimer_Rtmp_PublishTimeout`): FirstPacketTimeout (a publisher that sends nothing is closed after 100ms, before 200ms), NormalTimeout (closed at least 200ms after its last message), PauseShorterThanNormal (a 250ms pause after the first messages is kept), SteadyStreamKept (a real-time stream plays past the checks); Timer: KickedWhenIdle (a publisher with no player is closed after at least 0.3s), KickedAfterLastPlayerLeaves (a player keeps it past that, then it is closed at least 0.3s after the player leaves). Each close sends `on_unpublish` | Normal, Timer | |
| Rtmp | Common | Bad input: HandshakeBadVersion (C0 version 6: closed without S0), HandshakeShortC0C1, HandshakeShortC2 (a client that closes in the handshake); ChunkFresh1Accepted (a new chunk stream may start with format 1, like librtmp), ChunkFresh2Closed, ChunkFresh3Closed, ChunkFmt0MidMessage, ChunkLengthChanged, ChunkSizeTooSmall (64), MalformedCommand (closed); UnknownCommandAnswered (a `_result`), UnknownCommandWhilePublishing, UnknownCommandWhilePlaying (ignored, the stream goes on), CommandsBeforeConnect (dropped), PublishWithoutCreateStream (ignored, no status or `on_publish`), MediaBeforePublish (dropped); PublishMissingApp, PlayMissingApp (a tcUrl without an app uses `__defaultApp__`); PublisherCloseMidMessage (unpublished, gone from the API, publishable again), ConnectCloseMidMessage. Each closed case checks the server serves the next client | Normal | `srs_utest_manual_rtmp`, `srs_utest_manual_protocol` |
| Rtmps | Common | PublishPlay (the player verified the test certificate), PublishPlayHevc, ServesTestCertificate; HandshakeSimple, HandshakeComplex, ChunkSize128 inside TLS; StreamNaming (multi-level app and param: the `rtmps://` tcUrl in `on_publish`, an RTMP player finds the stream); PublishStatuses (FMLE publish and play replies); PublishRtmpsPlayRtmp, PublishRtmpPlayRtmps, PlayersOfBoth (3 RTMP and 3 RTMPS players of one stream); PlayBeforePublish, SecondPublisherRejected (RTMPS after RTMP), SecondPublisherRtmpRejected (RTMP after RTMPS), PublisherCloseCleanup; PlainRtmpOnRtmpsPort, BadTlsRecord (closed, at most after a TLS alert), RtmpsOnRtmpPort (the dial fails), CloseInTlsHandshake. Each bad-input case checks the server serves the next client | Normal | |
| Srt | Common | Signaling, with the Go client publishing the TS test file (its first 500ms of PCR at once, then real time) and an SRT player connected before the publish (H.264 High 768x320 and AAC 44.1 kHz stereo, one sequence header each, from the first keyframe, timestamps, A/V sync within ffmpeg's TS muxing, 400ms): PublishPlay (`#!::r=live/<s>,m=publish`: `on_publish` app `live`, the default vhost, no param, the stream URL, `srt://<host>/live`; `m=request` plays it); MultiLevelApp (`r=live/a/b/<s>`: app `live/a/b`, and it plays); ParamsInStreamId (other keys, `token=abc,ext=1`, are the params); QueryInResource (`r=live/<s>?token=abc`, the older way); HostIsVhost (`h=example.com`: the default vhost, and a player without `h` plays it); LegacyHostResource, LegacyHostVhostResource (the stream in `h` as `live/<s>` or `example.com/live/<s>`: app `live`, and it plays); ModeUppercase (`m=PUBLISH`); NoModeIsPlayer (`#!::` without `m`: `on_play`, no `on_publish`); ShortFormPlays (`live/<s>`, without `#!::`, is a player by default, `default_mode request`, and plays); ShortFormDefaultApp (`<s>` alone is in `live`, `default_app`); NoStreamIdUsesDefault (no streamid plays `live/livestream`, `default_streamid`); BadMode (`m=play`), NoResource (no `r`), HostOnly (`h` without `r`): the handshake succeeds, then SRS closes the connection before any hook, and serves the next caller; PassphraseNotExpected (a caller with a passphrase: refused with 1011, unsecure); FfmpegPublishPlays (ffmpeg with libsrt publishes, the Go client plays; skipped when the ffmpeg lacks libsrt). Media, SRS passing the TS on as it is: PublishPlayHevc (`hevc-aac.ts`: HEVC 768x320 and AAC); PublishPlayOpus (`h264-opus.ts`: H.264 and Opus 48 kHz stereo, which has no sequence header in TS). Lifecycle: ManyPlayers (4 players before the publish all play); SecondPublisherRejected (closed, the first plays on); RepublishPlays (once the first publisher has left the API, a new publish plays to a new player from its start, and the first publish's player stays, without `on_stop`); PublisherCloseCleanup (the publisher and its stream leave the API); PlayerCloseCleanup (`on_stop`, the player leaves the API, the stream counts 1 client). Latency, from the TSBPD delays of the handshake, each end holding what it receives for the larger of its own latency and the sender's peer latency: LatencyNone (`recvlatency 0` and `peerlatency` 0: none either way); LatencyFromCaller (a caller latency of 200ms applies both ways) | Normal | |
| Srt | Latencies | `recvlatency 250`, `peerlatency 150`, on the shared server `RemuxOff`: ServerLatencies (a caller without latency: SRS holds what it sends for 250ms and asks it to hold what SRS sends for 150ms); CallerBetween (200ms raises only the second) | Normal | |
| Srt | LatencyOnly | `latency 300` alone, which full.conf says sets both, on the shared server `Fmp4Keys`: LatencySetsBoth (known bug) | Normal | |
| Srt | Options | `default_mode publish`, `default_app srtapp`, `default_streamid #!::r=srtapp/default,m=request` and a passphrase with `pbkeylen 16`, on the shared server `TsFloor`: ShortFormPublishes (`live/<s>` publishes, and a player plays it over the encrypted connections); ShortFormDefaultApp (`<s>` publishes in `srtapp`); ResourceFormKeepsRequest (`#!::r=live/<s>` without `m` is still a player); DefaultStreamId (no streamid plays `srtapp/default`); PassphraseKeyLength32 (a caller's 32-byte key is accepted, and it plays); PassphraseWrong (refused with 1010, bad secret), PassphraseMissing (refused with 1011, unsecure): before any hook, and the server serves the next caller | Normal | |
| Srt | PeerIdle | `peer_idle_timeout 1000`, on the shared timer server `SegmentsKeep` (`TestTimer_Srt_PeerIdle`): IdlePublisherClosed (a publisher that sends its 500ms lead and then nothing: `on_unpublish` 1s later, closed, no longer publishing); IdlePlayerClosed (a player of a stream nobody publishes: `on_stop` 1s after `on_play`, closed); UnpublishClosesPlayer (a player learns that its publisher left only by being closed 1s later); StreamingKept (a publisher and a player that stream for 1.5s stay) | Timer | |
| Signal | Smoke | TermExitsZero, TermWithPublisherExitsZero (`TestTimer_Signal_Smoke`) | Timer | |
