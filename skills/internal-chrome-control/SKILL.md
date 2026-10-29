---
name: internal-chrome-control
description: Use when a task needs Chrome or Edge as a test client for SRS, such as publishing over WHIP or playing over WHEP, HTTP-FLV, HTTP-TS or HLS in a headless browser, or reading the browser's WebRTC stats in a manual or automated test.
---

# Chrome Control

Drive the installed Chrome or Edge with `puppeteer-core` as a test client for SRS.

- **No browser download**: it uses the browser already installed.
- **Never the user's browser**: every launch is a new headless browser with its own profile.
- **Fake media**: Chrome's fake camera and microphone, so no permission prompt on any OS.

## When to use

- Publish to SRS from a real browser over WHIP, with H.264, H.265, VP9 or AV1 video and Opus audio.
- Play from SRS in a real browser over WHEP, HTTP-FLV, HTTP-TS or HLS.
- Read what `chrome://webrtc-internals` shows, as `getStats()` samples once a second or as the page's
  own dump, such as NACK and RTX counters while the SRS drop simulator drops packets.
- Check what the browser supports, such as which codecs it can encode.

It is not for:

- The SRS player pages (`whip.html`, `whep.html`, `srs_player.html`): this skill has its own test page.
- Real cameras, screens or microphones: only the fake devices are used.

## Running the scripts

- `scripts/` and `page/` resolve relative to the directory containing this `SKILL.md`.
- Needs Node.js 22.12 or later. Install the pinned packages once, and again when `package-lock.json` changes:
  `npm ci --ignore-scripts` in this directory.
- Unit tests: `npm test` in this directory. They include one live run with the installed browser.
- The test page loads the players vendored in the SRS tree, `trunk/research/players/js`, so run the
  driver from an SRS checkout.

The browser is found in this order; `CHROME_PATH` overrides the search:

1. `CHROME_PATH`, which must be an executable; a wrong path is an error, not a fallback.
2. Google Chrome, then Microsoft Edge, then Chromium, in the OS's standard install places.

## Preflight

Before a test, check each step and stop at the first one that fails:

1. `node --version` is 22.12 or later, else the driver cannot run.
2. `node_modules/` exists and is newer than `package-lock.json`, else run `npm ci --ignore-scripts`.
3. `node scripts/chrome.js find-browser` prints a browser; exit 2 means there is none (a `SKIP`).
4. `node scripts/chrome.js launch --probe` prints `PASS launch` with the version and video codecs.
5. SRS answers on its HTTP API, and for WebRTC its candidate is the host's own IPv4.

## Commands

- `node scripts/chrome.js find-browser`: prints the browser executable.
- `node scripts/chrome.js launch --probe [--headed] [--log-dir <dir>]`: starts the browser, prints its
  version, pid and video codecs, then closes it.
  - `--probe` is required; it is the only thing `launch` does.
  - `--headed` shows the window, for a human watching.
  - `--log-dir` keeps the browser profile under that directory; without it, the profile is a temporary
    directory removed at exit. When Windows still holds it after 3 s, the driver prints
    `profile <dir> not removed` on stderr and keeps the result.
- `node scripts/chrome.js serve --port <n> [--host <h>]`: serves the test page until stopped, for a
  page opened by hand. It serves `page/` at `/` and the vendored players at `/players/`, nothing outside
  them.
  - `--port` is required; `0` picks a free port, and the `serving` line names it.
  - `--host` is the address to listen on, `127.0.0.1` by default.
- `node scripts/chrome.js run --mode <m> --url <u> [--codec <c>] [--name <n>] [--mode ...] [options]`:
  runs one or more jobs in one browser and checks that each stream moves.
  - Each `--mode` starts a job; `--url`, `--codec` and `--name` after it belong to that job. A job
    without `--name` is named after its mode, such as `whep`, `whip-vp9` or `flv-2`.
  - WHIP jobs open first. The players open once every publisher is `connected`, so they find the stream.
    A failed publisher keeps every player closed.
  - A job is ready when its page reaches `connected` (WHIP) or `playing` (the others), within
    `--timeout` seconds: 10 by default, 20 for an H.265 job and the players of its stream.
  - All jobs then sample for `--seconds` (default 5). A job passes when `framesEncoded` (WHIP),
    `framesDecoded` (WHEP) or `currentTime` (HTTP) rose. Players stop before publishers.
  - `--out <file>` writes the [stats file](#stats-files).
  - `--internals <file>` saves the `chrome://webrtc-internals` dump (see [Stats files](#stats-files)).
  - `--log-dir <dir>` keeps the browser profile there; `--page-port <n>` is the test page port on
    `127.0.0.1` (default: a free port); `--headed` shows the windows.
  - `--allow-loopback` lets the browser use loopback candidates, only for a host with no other IPv4.
  - Output: `PASS <job> ready ...`, `PASS <job> <metric> +<n> in <s>s`, `FAIL <job>: <reason>`, and
    `SKIP <job>: <reason>` for a codec the browser lacks and for the players of that stream.
  - `LOCATION <job> <url>` follows the ready line of a WHIP or WHEP job: the answer's `Location`. Its
    `session` query is the SRS session's username, which the NACK simulator API takes.
  - Before launching, it checks that every job's host and port accept a connection, and exits 6 if one
    does not.

Example: publish H.264 and play it back over WHEP, with the stats saved:

```bash
node scripts/chrome.js run \
  --mode whip --url "http://127.0.0.1:1985/rtc/v1/whip/?app=live&stream=t1&eip=192.168.1.10" \
  --mode whep --url "http://127.0.0.1:1985/rtc/v1/whep/?app=live&stream=t1&eip=192.168.1.10" \
  --seconds 5 --out /tmp/t1-stats.json
```

Exit codes:

- `0` pass, including jobs skipped for a codec the browser lacks.
- `1` fail, with a `FAIL` line.
- `2` skip, with a `SKIP` line: no browser, or `puppeteer-core` not installed.
- `3` bad arguments or a bad `CHROME_PATH`, with the usage.
- `6` SRS did not answer: a job's host and port refused the connection, or the WHIP or WHEP request got
  no answer.

## Modes

`page/index.html` with `page/srs-test.js` does one job per URL; `run` opens one window per job:

- `?mode=whip&url=<WHIP URL>[&codec=h264|h265|vp9|av1]`: publishes the fake camera and microphone. A codec
  is preferred in the offer, with RTX, and added to the URL as `vcodec`, which SRS needs to negotiate it.
  Without a codec SRS picks H.264.
- `?mode=whep&url=<WHEP URL>[&codec=...]`: plays into the page's `<video>`; SRS answers with the
  stream's codec.
- `?mode=flv&url=<URL>` and `?mode=ts&url=<URL>`: HTTP-FLV and HTTP-TS with the vendored `mpegts.js`.
- `?mode=hls&url=<URL>`: HLS with the vendored `hls.js`.

The page keeps everything in `window.srsTest`:

- `state`: `starting`, `signaling`, `connected` (ICE and DTLS up), `playing` (the `<video>` plays), then
  `stopped`; or `error` and `unsupported` (such as a codec the browser lacks), with the reason in `error`.
- `samples`: one per second; see [Stats files](#stats-files).
- `events`: state changes and player events, with milliseconds since load.
- `location`: the WHIP or WHEP resource from the answer's `Location`.
- `stop()`: takes a last sample, sends the WHIP or WHEP `DELETE`, and closes everything.

## Stats files

`--out <file>` is JSON, written for passed and failed runs:

- Top level: `command`, `browser`, `executablePath`, `startedAt`, `seconds`, `exitCode`, `jobs`, and `internals`
  with `--internals`.
- Each job: `name`, `mode`, `url`, `codec`, `result`, `reason`, `readyMs`, `state`, `error`,
  `location`, `events`, `samples`, and up to 100 `console` lines.

Each sample has `t`, milliseconds since the page loaded, `at`, the wall clock in epoch milliseconds (to
line samples up with an action taken outside the browser), and these groups; a group is `null` when the
report does not exist:

- `inboundVideo` (play): `codec`, `packetsReceived`, `packetsLost`, `nackCount`,
  `retransmittedPacketsReceived`, `retransmittedBytesReceived`, `framesDecoded`, `framesDropped`,
  `framesPerSecond`, `frameWidth`, `frameHeight`, `freezeCount`, `totalFreezesDuration`, `jitter`,
  `pliCount`.
- `inboundAudio` (play): `codec`, `packetsReceived`, `packetsLost`, `totalAudioEnergy`,
  `concealedSamples`, `totalSamplesReceived`, `jitter`.
- `outboundVideo` (publish): `codec`, `packetsSent`, `framesEncoded`, `nackCount`,
  `retransmittedPacketsSent`, `retransmittedBytesSent`, `pliCount`, `qualityLimitationReason`,
  `frameWidth`, `frameHeight`.
- `outboundAudio` (publish): `codec`, `packetsSent`, `nackCount`, `retransmittedPacketsSent`,
  `retransmittedBytesSent`.
- `pair`: the selected candidate pair's `state`, `nominated` and `currentRoundTripTime`.
- `media` (every play mode): the `<video>` element's `currentTime`, `videoWidth`, `videoHeight`,
  `paused`, `readyState`, `droppedVideoFrames`, `totalVideoFrames` and `audioDecodedBytes`.

`codec` is the codec's `mimeType`, such as `video/H264`. A field the browser does not report is `null`,
except the NACK and retransmission counters, which are `0` until Chrome reports them.

`--internals <file>` saves the `chrome://webrtc-internals` dump:

- A webrtc-internals window opens before the jobs; its "Download" dump is saved as JSON, the format the
  webrtc-internals importers read, with `PeerConnections`, `UserAgent` and `getUserMedia`.
- It is taken after `--seconds` and before the jobs stop, once the page knows every WebRTC job's peer
  connection (up to 10 s).
- A dump that is missing, or has fewer peer connections than the WebRTC jobs, prints
  `FAIL internals: <reason>` and fails the run. The stats file records it under `internals`.

## Safety

- The browser talks to the driver over a pipe (`pipe: true`) and opens no debugging port.
- The driver never connects to a running browser.
- Every launch has its own profile (`--user-data-dir`), so the user's sign-ins, history and extensions
  are never used.
- If the browser does not close within 5 s, the driver kills only the process it started.
- Headless by default; `--headed` only when a human watches.
- The page server listens on `127.0.0.1` and serves only the page and the players.

The driver passes these flags to the browser:

- `--use-fake-device-for-media-stream`: a moving test pattern (640x480 at 20 fps) and a beep.
- `--use-fake-ui-for-media-stream`: grants `getUserMedia` without a prompt.
- `--autoplay-policy=no-user-gesture-required`: plays unmuted audio without a click.
- `--disable-background-media-suspend`, `--disable-renderer-backgrounding`,
  `--disable-background-timer-throttling` and `--disable-backgrounding-occluded-windows`: windows side
  by side keep playing and sampling.
- `--no-first-run` and `--no-default-browser-check`: no welcome page or dialogs.
- `--allow-loopback-in-peer-connection`: only with `run --allow-loopback`.

## Windows and Edge

- On Windows the lookup finds Edge, such as
  `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`, when Chrome is not installed.
- Install Node.js LTS with `winget install OpenJS.NodeJS.LTS`; open a new shell so `node` is on the PATH.
- Run the same commands with `node scripts\chrome.js ...` or forward slashes; `npm test` quotes its glob
  so Node expands it on every OS.
- Edge is Chromium, so the flags, fake devices and `chrome://webrtc-internals` are the same; check the
  `--internals` dump and the codec list with `launch --probe` on a new Edge version.
- Edge 156 on Windows has no H.265 in its send capabilities, so a WHIP job with `--codec h265` is a `SKIP`.
- On Linux, set `CHROME_PATH` if Chrome is not in `/usr/bin` or `/opt/google/chrome`. Google Chrome has
  no Linux arm64 build, so on an arm64 Mac run SRS in Docker and the browser on the Mac.

## Testing SRS

The `srs-develop` skill tests SRS with this driver through Bash scripts in `skills/srs-develop/scripts/`:

- **`chrome-webrtc-test.sh`**: WHIP H.264/Opus played back over WHEP and HTTP-FLV, and WHEP of an RTMP
  stream.
- **`chrome-loss-test.sh`**: NACK and RTX under loss from the SRS drop simulator, downlink and uplink.
- **`chrome-http-test.sh`**: HTTP-FLV, HTTP-TS and HLS playback.
- **`chrome-codec-test.sh`**: WHIP and WHEP with H.264, VP9, AV1 and H.265, each with Opus.
- **`chrome-test-lib.sh`**: the helper they source. It starts SRS, runs the driver and checks the stats
  files with `jq`; `chrome-test-lib-test.sh` tests it.

How they run:

- **Lane I**: `chrome-lane-test.sh` runs this skill's `npm test` and the helper's self-test, then the four
  cases in parallel, as lane I of the Full tier.
- **SRS on Linux**: `chrome-linux-test.sh` runs the same cases against SRS for Linux in Docker. The
  GitHub Actions `chrome-test` job runs it with Chrome on Linux.
- **SRS on Windows**: `chrome-windows-test.sh` runs the same cases from Edge on the Windows test host to
  the native `srs.exe`.
- **`SRS_TEST_CHROME=0`** skips them all.

`skills/srs-develop/references/integration-tests.md` has the rest: each case's checks, its `SKIP`, `KNOWN`
and `INFO` lines, the lanes, the times and the ports.
