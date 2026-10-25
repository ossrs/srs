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
make integ-unit     # The test media, then the unit tests of the clients, analyzers and harness, every test outside cases/.
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
cd trunk && ./configure --srt=on --gb28181=on --rtsp=on --utest=on --sanitizer=on --ffmpeg-opus=off --simulator=on && make
```

On Windows:

- The cases run `trunk/objs/windows/srs.exe`, which
  `skills/srs-develop/scripts/srs-windows-runner.sh build` builds without the
  sanitizer.
- Every server has only the listeners of the features it is built with.
- Windows has no signals, so the suite starts each server in a process group
  of its own and sends SIGTERM as Ctrl+Break.
- The runner's `integ [pattern]` step runs the suite there.

Flags, after `-args`:

- `-integ.srs <path>`: another SRS binary.
- `-integ.timer`: allocate ports from the timer process's part of the range; `make integ` sets it for the timer process.
- `-integ.known-bugs fail`: known-bug cases fail like normal cases; the default, `pass`, lets a known bug that fails pass.
- `-integ.scale N`: run each case N times as separate cases, named `<Case>`, `<Case>-2` to `<Case>-N`, each with its own stream on its group's server; timer cases run once, since each starts a server of its own. The scale check.
- `-integ.shards K`: split Common into K identical servers; a case gets the one its name picks. The default is 4: every case starts at once, and one single-threaded Common made the slowest real-time cases miss their 500ms limit while it was saturated in the first part of a run; 2 held until the HLS cases added their bursts, 3 until the suite had about 1,300 cases (Common cases at 570-600ms in a fast run).
- `-integ.settle D`: start the Fast and Normal cases no earlier than D after the process starts; timer cases start at once. `make integ` sets 2s for the timer process: it starts every case at once, beside the main process's first cases, and its few short cases missed their limit in that burst (up to 670ms for cases of 110ms alone). Its 5s timer cases set its run time, so the wait costs nothing.
- `-integ.limit-scale F`: multiply every class limit by F, at least 1, and the time the hook, security, edge and forward cases wait for SRS to accept or refuse a client, the stats cases for its frame counts, and the shared players for the media: HLS, DASH, RTSP, SRT, and RTMP where a case gives it a set time. CI runs with 4: its runners are slower and have fewer cores, and a case that hangs still fails.

`make integ`, `make integ-main` and `make integ-timer` pass `INTEG_ARGS` after
`-args`. The scale check runs about 1,000 cases:

```bash
make integ-main INTEG_ARGS="-integ.scale=56"
```

CI runs the suite in the `trunk/Dockerfile.test` image, as the `integ` job of
`.github/workflows/test.yml` does, with the media directory mounted so the
media is made once, and the main and timer cases one after the other, since a
runner has few cores:

```bash
docker build --tag srs:test -f trunk/Dockerfile.test .
M="-v $PWD/cmd/integ/testdata/media:/srs/cmd/integ/testdata/media"
docker run --rm -w /srs --env FFMPEG=/usr/local/bin/ffmpeg7 $M srs:test make integ-prepare
docker run --rm -w /srs --env FFMPEG=/usr/local/bin/ffmpeg7 $M srs:test make integ-unit
docker run --rm -w /srs --env FFMPEG=/usr/local/bin/ffmpeg7 $M srs:test make integ-main INTEG_ARGS=-integ.limit-scale=4
docker run --rm -w /srs --env FFMPEG=/usr/local/bin/ffmpeg7 $M srs:test make integ-timer INTEG_ARGS=-integ.limit-scale=4
```

The image's `ffmpeg` is 5.0, which writes HEVC in FLV the legacy way, so the
test media, and the cases that run ffmpeg, use its `ffmpeg7`; it has no VP9 or AV1 encoder, so those
cases skip. The Linux-only cases, such as the `/proc` values of the API, run
there.

A full run has about 75 SRS at once, each a sanitizer build, so the adapter
bounds ASan's quarantine, the freed memory it holds before it reuses it, to
16 MB per server (`ASAN_OPTIONS`, set before the user's own, so theirs win
where they overlap); with the default 256 MB the servers' memory reached
3.4 GB in a Linux run, and the Docker VM's OOM killer ended a Common shard.
With the bound a run takes about 2.5 GB, so Docker Desktop's default 4 GB VM
holds it. A server the OOM killer ended fails its group with "SRS was killed
by SIGKILL", which the suite never sends to a running server; on Linux,
confirm it in the kernel log (`dmesg`, in a privileged container for the
Docker VM: `docker run --rm --privileged srs:test dmesg | grep -i 'killed
process'`) before taking the group's failures for flakes.

Each `go test` process ends with its summary: the cases passed, failed and
skipped with their reasons, the known bugs, and the 10 slowest cases. Then the
SRS processes it started and the most that ran at once, the ports it took, and
the CPU each Common server used, as a share of the one core SRS runs on. Run it in
`cmd/integ/cases` with no package argument, as above, or `go test` hides the
summary of a passing run.

Each run works in `/tmp/srs-integ/<pid>/`, or `%TEMP%\srs-integ\<pid>\` on
Windows, one directory per server with its env, pid file and console log. A
group's directory is removed when it passes and kept when it fails; a failing
case shows the last 30 lines of the log. The run removes its directory when
every test passed, and the directories of earlier runs whose process is gone.

The CLI gives the stream tools by hand:

```bash
bin/srs-integ probe <url>            # Print the stream report of rtmp://, rtmps://, http://...{flv,ts,aac,mp3,m3u8,mpd}, a WHEP URL (http://host:1985/rtc/v1/whep/?app=live&stream=s, offering every codec), srt://host:port?streamid=#!::r=live/s,m=request (also passphrase, pbkeylen and latency in ms), rtsp://host:port/app/stream (over TCP) or an FLV, TS, AAC, MP3 or MP4 file; -duration, -timeout.
bin/srs-integ publish <file> <url>   # Publish an FLV file in real time, to rtmp:// or rtmps://, or an MP4 file with H.264, HEVC, VP9 or AV1 and Opus by WHIP (http://host:1985/rtc/v1/whip/?app=live&stream=s; it adds vcodec for a codec other than H.264), or an MPEG-TS file to srt://host:port?streamid=#!::r=live/s,m=publish, timed by its PCR. Neither verifies an rtmps:// certificate, like ffmpeg.
bin/srs-integ prepare -h             # Make the test media; its flags.
```

## Layout

| Path | What it holds |
|---|---|
| `main.go` | The CLI: `prepare`, `publish`, `probe` |
| `internal/server/` | The adapter, the only package that knows the SRS binary, its env vars, ports and paths |
| `internal/check/` | The case helper |
| `internal/media/` | The test media, its prepare cache, FLV and MP4 readers |
| `internal/report/` | The stream report every analyzer fills, and `Until` |
| `internal/mock/` | The HTTP server for hooks, heartbeat and forward backend |
| `internal/metrics/` | A strict Prometheus text parser |
| `internal/rtmp/` | RTMP and RTMPS client |
| `internal/httpplay/` | The HTTP stream player the FLV, TS and audio players share |
| `internal/flv/`, `internal/ts/`, `internal/audio/`, `internal/mp4/` | Demuxers, analyzers and players of FLV, TS, AAC/MP3 and MP4 |
| `internal/hls/`, `internal/dash/` | HLS and DASH parsers and players |
| `internal/rtc/` | WebRTC WHIP and WHEP client on pion, with raw and recorder tools |
| `internal/srt/` | SRT caller on gosrt |
| `internal/rtsp/` | RTSP player on gortsplib |
| `internal/gb/` | GB28181 PS-over-RTP publisher, without SIP |
| `cases/` | The cases |
| `testdata/` | The test TLS certificate; generated media in `testdata/media/` |

Each internal package has its own unit tests.

## Rules

### Black box

A case observes SRS only through the network (RTMP, HTTP, WebRTC, SRT, RTSP,
UDP), the HTTP API and the callbacks it sends, the files it writes, its exit
code and the signals the case sends it. No SRS source, internals or log text
decides pass or fail; the log is attached only to explain a failure. The
cases of the log settings (`TestEnv_*`) and of the log's reopen
(`TestTimer_Signal_LogFile`, `TestTimer_Signal_Console`) are the one
exception: the log is what those settings and that reopen change, so they
read where it goes and the header of each line, its time, level and pid,
never its text.

### Env only

SRS starts as `objs/srs -e` with `SRS_*` env vars, never with a config file. A
feature that needs a file, such as several vhosts, gets no case. The one
exception is ingest, transcode and exec, which have no env vars: their
groups set `Config.Vhost`, and the adapter writes a file of only the
default vhost and the FFmpeg log dir, for `-c`, while the env sets the rest.

### Naming

- **Normal cases:** `Test<Feature>_<Group>/<Case>`, such as `TestRtmp_Common/PlayBeforePublish`.
- **Timer cases:** `TestTimer_<Feature>_<Group>/<Case>`, such as `TestTimer_Hls_Dispose/FilesRemoved`.
- **Group:** `Common`, the one shared server with every feature on, or the name of a variant server.
- **Feature:** one name per feature area, such as `Rtmp`, `HttpFlv`, `Hls`, `Rtc`, `Api` or `Hooks`. Reuse a name from the catalog before adding one.

`make integ` runs `^TestTimer_` in one process and everything else in another,
with `-parallel 24`, at the same time. With 64 cases at once, the cases that
start together slowed each other past their limit, while the run took no
less time; with 32, at about 1,300 cases, a fast run still had cases at
500-600ms, and 24 holds them under 420ms in about the same time.

### Servers

- **Common:** one SRS with every feature that doesn't change how other streams behave: RTMP, RTMPS, the HTTP API and server, HTTPS, WebRTC, SRT, RTSP, GB28181, the exporter, HTTP-FLV, HLS, DASH, and every hook to the mock. It plays at the lowest latency. It runs as 4 shards (`-integ.shards`); a case's name picks its shard, and each case uses its own stream name.
- **Variant:** a server of its own, only when Common can't do it: a value Common can't have, global state, a process change, or a topology (edge, origin cluster, forward). The `server.With*` and `Config` options build its config.
- **Shared variant:** when one config serves the groups of several features, they share it with `server.Shared(t, name, cfg)`, such as `RemuxOff`, `TsFloor`, `Fmp4Keys`, `TsMount`, `SegmentsKeep` and `SegmentsDispose`.
- **Hook mock:** each server's hooks call one mock, which accepts every request unless a case scripts a reply for its own stream, such as `Hooks.Script(mock.Match{Action: "on_publish", Stream: stream}, mock.Reject(403))`.
- **No GOP cache:** SRS keeps no GOP for WebRTC, SRT or RTSP, so a case connects its player before the media comes (`sendRTC`, `srtPlayer`, `rtspPublisher`).
- **Bursts:** SRS runs on one thread, so a case that sends a burst at once holds one of a server's 2 burst slots (`srs.Burst(c)`), out of its case time.
- **Command runs:** a case that runs the binary as a command (`-v`, `-t`, a bad env) uses `server.Exec`, in a dir of its own with every listener off, and checks only its exit code.

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
| `h264-slices-aac.flv` | H.264 Baseline, 2 slices a frame | AAC | GB28181 frames of several slices |
| `h264-baseline-opus.mp4` | H.264 Baseline | Opus 48 kHz stereo | WHIP |
| `hevc-aac.flv` | HEVC Main | AAC | enhanced RTMP |
| `hevc-opus.mp4` | HEVC Main, no B-frames | Opus | WebRTC |
| `h264-high-aac.ts` | H.264 High with B-frames | AAC 44.1 kHz stereo | SRT, in MPEG-TS; its unit test is in `internal/ts` |
| `hevc-aac.ts` | HEVC Main with B-frames | AAC 44.1 kHz stereo | SRT, in MPEG-TS; its unit test is in `internal/ts` |
| `h264-opus.ts` | H.264 High with B-frames | Opus 48 kHz stereo | SRT, in MPEG-TS; its unit test is in `internal/ts` |
| `aac-only.ts`, `h264-only.ts` | none, or H.264 High | AAC, or none | SRT with one track, in MPEG-TS |
| `aac-48k-packed.ts` | none | AAC 48 kHz stereo, several frames to a PES | SRT frame times at a rate FLV can't signal, in MPEG-TS |
| `aac-only.flv` | none | AAC | audio only |
| `h264-only.flv` | H.264 High | none | video only |
| `h264-high-mp3.flv` | H.264 High | MP3 44.1 kHz stereo | MP3 |
| `vp9-opus.mp4` | VP9 profile 0 | Opus | WebRTC |
| `av1-opus.mp4` | AV1 Main | Opus | WebRTC |
| `pcmu.ul`, `pcma.al` | none | raw G.711 mu-law or A-law, 8 kHz mono | WebRTC, with the video of `h264-baseline-opus.mp4` |

- **ffmpeg:** `$FFMPEG`, then `PATH`, then `~/.local/bin`, the first with libx264, libx265 and an Opus encoder (libopus, or the native `opus`). Cases use the same lookup.
- **macOS:** when no ffmpeg is found, or it lacks libvpx-vp9 or libsvtav1, prepare runs `skills/srs-develop/scripts/setup-ffmpeg-with-whip.sh` to build one in `~/.local/bin`. Other tests share that binary, so it first waits until the Full-tier lock is free and no ffmpeg runs.
- **Unavailable:** a recipe whose encoder the ffmpeg lacks, such as VP9 and AV1 in CI, is recorded as unavailable, and its cases skip with "ffmpeg lacks <encoder>".
- **MPEG-TS** files put each audio frame in a PES of its own (`-pes_payload_size 0`), as SRS's SRT docs publish TS, so the audio is within about 190ms of the video of the same time in file order; ffmpeg's default packs several frames into one, about 300ms late, which only `aac-48k-packed.ts` keeps.
- **Cache:** `testdata/media/manifest.json` records the source's size and SHA-256, each recipe and the ffmpeg version. Only a file whose entry changed is made again.
- **Missing or stale:** a case gets a file with `media.Path(c, media.H264HighAAC)`, which fails with "run make integ-prepare" when the file is missing or stale. It never skips for that.
- **Checks:** ffmpeg versions make different bytes, so nothing checks bytes. The unit tests of `internal/media`, which need `make integ-prepare` first, read each file and check its codecs, profile, B-frames, GOP, frame rate, sample rate, duration and frame counts.

### Known bugs

A case that finds an SRS bug is marked `KnownBug(c, "<reason>")`, not fixed or
skipped. It still runs: a failure passes and logs `KNOWN BUG: <reason>`, and a
pass fails with "bug fixed, remove the KnownBug mark".
`-integ.known-bugs=fail` makes them fail like normal cases.
A bug that shows only on some systems, in a race, or with some test media is
marked only when the case sees it, such as the Linux-only ones.

### Skip list

A case that cannot pass on an OS by design, such as one that sends a signal
other than SIGTERM on Windows, is on the skip list in
`internal/check/skips.go`: an OS, a pattern of the case names and the reason.
It does not run on that OS, and the summary lists it as skipped with
`not on <os>: <reason>`. A case that fails there for a bug gets a fix, not an
entry. A feature that a build lacks for now, such as GB28181 and RTSP on
Windows, has an entry until the build has it.

### Ports

The suite owns ports 14000–14999: the main process allocates from 14000–14799
and the timer process from 14800–14999. `SRS_INTEG_PORTS` moves or widens the
range. A server gets a port only for each listener it turns on, and one for
its hook mock when it has one, never an SRS default port, and a port is never
handed out twice in a run. When the range runs out, the run fails with "port
range exhausted, widen SRS_INTEG_PORTS".

## Conversion matrix

Each publish protocol to each play protocol, for each codec the pair carries:
one case per cell, `TestConvert_<Group>/<Source>To<Player>_<Codec>`, such as
`TestConvert_Common/RtmpToHlsTs_HevcAac` (`cases/convert*_test.go`). The
player's report must match the media, or SRS refuses the player, or the cell
says what it gets.

| Source | RTMP, HTTP-FLV | HTTP-TS, HLS, DASH | HTTP MP3 / AAC | WHEP | SRT | RTSP |
|---|---|---|---|---|---|---|
| RTMP, RTMPS | as sent | as sent | its audio track | AAC to Opus, MP3 dropped (`rtmp_to_rtc`) | not converted | as sent, MP3 dropped |
| WHIP | Opus to AAC (`rtc_to_rtmp`) | Opus to AAC | AAC only | as sent | not converted | not converted |
| SRT | as sent, Opus dropped | as sent, Opus dropped | its audio track | AAC to Opus | as sent | not converted |
| GB28181 | as sent | as sent | AAC only | AAC to Opus | not converted | as sent |

- **Codecs:** H.264 and HEVC with AAC, MP3 or Opus, and audio-only or video-only streams. An audio mount of the other codec ends with no frame. SRS never transcodes video: VP9, AV1 and G.711 from WHIP are relay-only, and a WHEP player that can't take the stream's video codec gets 400.
- **Groups:** most cells run on `Common`; WHEP and HTTP-TS on `TsMount` (`rtmp_to_rtc` on), HTTP MP3 and AAC on `Mp3Mount` and `AacMount`, HLS fmp4 on `Fmp4`, WHIP sources on `RtcToRtmp`, GB28181 segments on `GbSegments`.
- **Players:** RTMP and HTTP players join 200ms into a real-time stream; HLS and DASH play a stream published at once; WHEP and RTSP connect before the media, since SRS keeps no GOP for them.

## Catalog

One row per feature: its groups and what they cover. The cases and their
checks are in the test files under `cases/`.

| Feature | Groups | Covers |
|---|---|---|
| Api | Smoke, Common, BasicOnePort, Bearer, BearerApiOnly, AuthConfig, Kick, Stats, TsMount, Mp3Mount, AacMount, Bytes, Frames, RawApi | System endpoints, vhosts, streams and clients, kicking clients, stats and counters, the exporter, the HTTPS API, basic and bearer auth, CORS, the RAW API |
| Reload | RawApi, Sighup | Reload by the RAW API and by SIGHUP |
| Heartbeat | Full, Defaults, NoPath, Config | Heartbeat requests, fields, interval and refused settings |
| Rtmp | Common, GopCacheOff, GopCacheMaxFrames, QueueDefault, QueueLength, ReduceSequenceHeader, JitterZero, JitterOff, AtcMixCorrect, MergedWrite, SendMinInterval, PublishTimeout | Signaling, media, lifecycle, GOP cache, queue, timestamps and jitter, latency, publish timeouts, bad input |
| Rtmps | Common | RTMP over TLS: publish, play, handshake and naming |
| HttpFlv | Common, NoGuess, NoAudio, NoVideoKeep, RemuxOff | HTTP-FLV and HTTPS-FLV play, the guessed FLV header, `has_audio`/`has_video`, CORS, lifecycle, bad input |
| HttpTs | TsMount, NoAudio, NoVideo | HTTP-TS play and its PMT |
| HttpAudio | AacMount, Mp3Mount, AacFastCache, Mp3FastCache | HTTP AAC and MP3 play and `fast_cache` |
| Https | Common | The HTTPS server's TLS |
| Hls | Common, Options, Fmp4, Fmp4Default, TsFloor, Keys, Fmp4Keys, Sessions, CtxOff, Dispose, Fmp4Dispose, Cleanup | HLS TS and fMP4, its options and file names, encryption, `hls_ctx` sessions, dispose and cleanup |
| Dash | Common, Options, StreamDir, Dispose, Cleanup | DASH play, the MPD, its options, dispose and cleanup |
| Rtc | Common, Transport, PreferRtx, NackTwccOff, StartFails | WHIP and WHEP: signaling, media, transport, a client that moves to another port, NACK, RTX, TWCC, a failed start |
| Srt | Common, Latencies, LatencyOnly, Options, PeerIdle | SRT publish and play, latencies, stream ids, encryption, idle peers |
| Rtsp | Common, Options, VhostOff | RTSP play from RTMP, its options |
| Gb | Common, Reconnect, MediaTimeout, RtmpOutput | GB28181 publish over TCP, reconnects, media timeouts, its RTMP output |
| Caster | UdpTs, UdpTsRepush, UdpTsOutput | The MPEG-TS over UDP caster |
| Convert | Common, RtcToRtmp, GbSegments, TsMount, Mp3Mount, AacMount, Fmp4, Reports | The conversion matrix (see Conversion matrix) and WebRTC Sender Reports |
| Hooks | Common, TsMount, Mp3Mount, AacMount, Down, Several, HlsSession | Every HTTP hook's payload, the client's type in the publish and play hooks, refusals, a hook that is down, several hook URLs, HLS sessions |
| Security | DenyPublish, DenyPlay, AllowOthers, NoRules, Common | Allow and deny rules for each protocol, a second publisher over another protocol |
| Forward | Destination, OneDestination, Off, Backend, Retry | Forward to destinations, by a backend, and its retry |
| Edge | Rtmp, Flv, Failover | Edge pull over RTMP and HTTP-FLV, edge publish, origin failover |
| Cluster | Origins | The origin cluster and its edge |
| Dvr | Session, SessionMp4, Segment, SegmentNoWait, Apply | DVR to FLV and MP4, session and segment plans, `dvr_apply`, `on_dvr` |
| HttpStatic | Common, RemuxOff, CtxOff | Static files, ranges, traversal, MIME types, VOD of FLV, MP4 and HLS |
| Signal | Quit, LogFile, Console | Quit signals, log reopen by SIGUSR1 and by the RAW API |
| Process | Cli | The binary's command line, config test and exit codes |
| Ingest | File | Ingest of a file by ffmpeg, as copy |
| Transcode | Copy | Transcode by ffmpeg, as copy, and its stop with the source |
| Exec | Publish | ffmpeg run on publish, writing the stream to a file |
| Env | Global, Cli | Global `SRS_*` env settings, log settings at start |
