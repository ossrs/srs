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
make integ          # Every case: the timer cases and the others, as two go test processes at once.
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

Each `go test` process ends with its summary: the cases passed, failed and
skipped with their reasons, the known bugs, and the 10 slowest cases. Run it in
`cmd/integ/cases` with no package argument, as above, or `go test` hides the
summary of a passing run.

Each run works in `/tmp/srs-integ/<pid>/`, one directory per server with its
env, pid file and console log. A group's directory is removed when it passes
and kept when it fails; a failing case shows the last 30 lines of the log. The
run removes its directory when every test passed, and the directories of
earlier runs whose process is gone.

The CLI gives the stream tools by hand:

```bash
bin/srs-integ probe <url>            # Read a stream and print its stream report.
bin/srs-integ publish <file> <url>   # Publish a media file in real time.
```

## Layout

- `main.go`: the CLI.
- `internal/server/`: the adapter, the only package that knows the SRS binary, its env var names, its ports and its paths.
- `internal/media/`: the test media.
- `internal/rtmp/`, `internal/flv/`, `internal/ts/`, `internal/mp4/`, `internal/hls/`, `internal/dash/`, `internal/rtc/`, `internal/srt/`, `internal/rtsp/`, `internal/gb/`: the clients and analyzers.
- `internal/mock/`: the hook and heartbeat servers.
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

- **Common:** one SRS with every feature that doesn't change how other streams behave, started by the first case that uses it and stopped after every test ends. Each case uses its own stream name.
  Its listeners: RTMP, RTMPS, the HTTP API and server, HTTPS for both, WebRTC over UDP and TCP, SRT, RTSP, the exporter, and the GB28181 media port. The MPEG-TS over UDP caster needs a variant, because a server has one stream caster.
- **Variant:** a server only when Common can't do it: a value Common can't have, global state, a process change, or a topology. A variant group is one top-level test that starts its SRS, runs its cases as parallel subtests, and stops the SRS.

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
not wall time.

### Known bugs

A case that finds an SRS bug is marked `KnownBug(c, "<reason>")`, not fixed or
skipped. It still runs: a failure passes and logs `KNOWN BUG: <reason>`, and a
pass fails with "bug fixed, remove the KnownBug mark".
`-integ.known-bugs=fail` makes them fail like normal cases.

### Ports

The suite owns ports 14000–14999: the main process allocates from 14000–14799
and the timer process from 14800–14999. `SRS_INTEG_PORTS` moves or widens the
range. A server gets a port only for each listener it turns on, never an SRS
default port, and a port is never handed out twice in a run. When the range
runs out, the run fails with "port range exhausted, widen SRS_INTEG_PORTS".

## Catalog

One row per group: the feature, the group, its cases, their class, and the
utest scenario it ports, if any.

| Feature | Group | Cases | Class | Ports utest scenario |
|---|---|---|---|---|
| Api | Smoke | Versions, BuildFeatures, OnlyItsListener | Fast | |
| Api | Common | Versions, ListenersAccept | Fast | |
| Signal | Smoke | TermExitsZero (`TestTimer_Signal_Smoke`) | Timer | |
