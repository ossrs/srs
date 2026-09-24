---
name: obs-control
description: Drive OBS Studio from the command line to publish test streams to SRS (Simple Realtime Server) or any RTMP/SRT/WHIP endpoint. Use whenever a task involves OBS Studio in any way - starting or stopping OBS, enabling or using the obs-websocket plugin, creating or switching profiles, scene collections, scenes or sources, setting the stream server/key, starting or stopping streaming or recording, reading OBS stats or screenshots, editing OBS config files (basic.ini, service.json, scene JSON), or testing SRS/Oryx with a real OBS publisher over RTMP, SRT or WebRTC WHIP. Also use when the user says "OBS", "broadcaster", "obs-websocket", "stream from OBS", "OBS profile", "OBS scene", or wants an automated/AI-controlled OBS for media-server testing, or several OBS publishers running in parallel, even if they do not say "skill" or "automation".
---

# OBS Control

Control OBS Studio programmatically so it can act as a repeatable publisher for testing SRS. Two control
surfaces exist and this skill uses both, in this order of preference:

1. **obs-websocket 5** (bundled with OBS 28+, port 4455): everything while OBS runs - profiles, scene
   collections, scenes, inputs, stream service, start/stop, status, screenshots.
2. **Config files** (`basic.ini`, `service.json`, scene collection JSON, `user.ini`): pre-seeding a machine
   before OBS starts and the few things the websocket cannot do (remove a scene collection, change output
   mode without a profile switch). Edit them only while OBS is not running; OBS overwrites the current
   profile and collection from memory.

The **obs-websocket plugin is required**. It ships inside OBS (`obs-websocket.plugin` / `.dll` / `.so`)
so nothing is downloaded; it only has to be *enabled* in its `config.json`, which OBS reads at startup.
Step 1 checks this and fixes it.

## Path resolution

- `scripts/` and `references/` resolve relative to the directory containing this `SKILL.md`.
- Run scripts with `python3` (3.9+, standard library only; no pip packages). They work on macOS, Linux and
  Windows and locate OBS and its config directory themselves (`OBS_BINARY`, `OBS_CONFIG_DIR`,
  `OBS_WS_HOST/PORT/PASSWORD` override). Every script bounds its own waits, so do not wrap them in coreutils
  `timeout` (absent on macOS).
- The user's SRS repository is the current working directory when this skill runs inside it; SRS itself may
  be a local build (`trunk/objs/srs`), Docker (`ossrs/srs`) or remote. Never `cd` away permanently.

## Parallel tests: one isolated instance per test

One OBS has one stream output, one current profile and one scene collection, so two tests can never share
an OBS. For anything that may run next to another test (several agents, a matrix of protocols, 10 OBS at
once), give each test its own **instance** with `--instance ID` (or `OBS_INSTANCE=ID`) on every command:

```bash
ID=$(python3 scripts/obsws.py instance new)             # claims a unique id, e.g. t3f9a2
python3 scripts/obsws.py launch --instance $ID --wait 90 --minimize-to-tray
python3 scripts/obs_srs_test.py setup --instance $ID --rtmp 'rtmp://localhost/live/{instance}' --start --verify
python3 scripts/obs_srs_test.py stream status --instance $ID
python3 scripts/obsws.py instance list                   # id, pid, websocket port, config dir
python3 scripts/obsws.py instance remove --instance $ID  # quit it and delete its config
```

**Always get the id from `instance new`; never invent one.** Hand-picked ids such as `t1` collide when two
agents or test runs pick the same one, and then both drive one OBS. `instance new` makes `t` plus 5 random hex
digits and claims it by atomically creating its directory, so an id already taken on this machine is
skipped and ids generated at the same moment are still different. Across machines that publish to one
SRS, the stream-name check below catches the rare repeat. `launch` refuses an instance that is already running unless
`--reuse` says this test launched it. Keep the id for the whole test (an agent whose shell does not
persist variables should write the printed id literally into later commands) and remove the instance at
the end. `instance remove --all` removes the instances of *other* running tests too; use it only when
nothing else is testing.

What an instance isolates, and why each part matters:

- **Config dir** `~/.cache/obs-control/instances/<id>/` (`OBS_CONTROL_HOME` moves the root). OBS derives
  it from `CFFIXED_USER_HOME` on macOS and `XDG_CONFIG_HOME` on Linux, so profiles, scene collections,
  `user.ini` (current profile/collection), crash sentinels, logs and browser-source cache are private.
  The user's own OBS config is never read or written. Windows OBS has no such override: not supported.
- **Websocket port and password**, allocated under a lock from 4460 upward, skipping ports that other
  instances or the main OBS use, and stored in the instance's `config.json`. `OBS_WS_*` variables are
  ignored in instance mode because they describe the main OBS.
- **Process**: a pid file per instance, so `quit` sends SIGTERM to exactly that OBS (the AppleScript
  quit addresses the bundle id, which every instance shares). `--multi` is added automatically so no
  "already running" dialog appears, and the main OBS's `probe`/`quit`/`launch` skip instance processes.
- **Startup dialogs**: a new instance is seeded with `FirstRun=true`, `ConfirmOnExit=false`, the main
  install's `LastVersion` and `MacOSPermissionsDialogLastShown`, so no wizard, permission or exit dialog blocks it.
- **Names**: profile, collection, scene and sources get the id (`SRS Test t3f9a2`,
  `SRS Media t3f9a2`), so logs, screenshots and SRS clients show which test they belong to.
- **SRS stream name**: `{instance}` in `--rtmp/--srt/--whip/--stream` expands to the id. Two publishers
  on one SRS stream collide there even with isolated OBS, so `setup --start` refuses a stream that SRS
  already shows as published by another client.
- **Shared temp files** (the ffmpeg test clip, the clock page) are written to a temp file and renamed, so
  a parallel setup never loads a half-written file.

Ten x264 encoders at
720p30 saturate a laptop CPU; for many instances lower `--width/--height/--video-bitrate` or use
`--encoder apple_h264` (hardware sessions are limited) and check `GetStats` for skipped frames.
Without `--instance` the scripts drive the user's main OBS with the plain names, as before; use that only
for a single interactive test.

## Step 1: Preflight (always)

```bash
python3 scripts/obsws.py probe            # add --instance ID for an isolated instance
```

It prints the OBS binary and version, config directory, profile/collection files on disk, websocket plugin
file, whether the server is enabled, whether OBS is running, stale crash sentinels, and the result of a
live handshake (including OBS's own profile and collection lists, which are authoritative while it runs;
the on-disk lists can lag). The last line says what to do next. Act on it:

| probe says | do |
| --- | --- |
| OBS not installed | ask the user to install OBS Studio 28+ (obs-websocket is included); do not try to build it |
| websocket disabled | `python3 scripts/obsws.py enable-websocket` then restart OBS: `quit` + `launch --wait 90` (or `enable-websocket --restart`) |
| OBS not running | `python3 scripts/obsws.py launch --wait 90` (add `--profile/--collection/--scene`, `--minimize-to-tray`); an instance's `launch` also creates its config |
| running but not answering | a modal dialog is probably open (crash recovery, permissions, exit confirmation); look at the newest log, ask the user to dismiss it, or `quit --force` + `launch` |
| READY | continue |

Why the restart: obs-websocket reads `plugin_config/obs-websocket/config.json` once in `obs_module_load`.
`launch` also removes stale `.sentinel/run_*` files (they trigger a blocking "unclean shutdown" dialog) and
passes `--disable-updater --disable-missing-files-check` so no dialog steals the UI thread.

Restarting OBS interrupts anything the user is doing in it. When OBS is already running with the websocket
disabled and the user did not ask for a restart, tell them what you are about to do first.

## Step 2: Build the test setup

For the common case (publish a synthetic scene to SRS) use the recipe, which is idempotent and safe to
re-run with different options:

```bash
# RTMP
python3 scripts/obs_srs_test.py setup --rtmp rtmp://localhost/live/livestream --start --verify
# WHIP (WebRTC); adds eip=127.0.0.1 for localhost so an SRS in Docker advertises a reachable candidate
python3 scripts/obs_srs_test.py setup --whip 'http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream' --start --verify
# SRT
python3 scripts/obs_srs_test.py setup --srt 'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish' --srs-api http://localhost:1985 --start --verify
# defaults: profile/collection "SRS Test", scene "SRS Scene", 1280x720@30, x264 veryfast 2000 kbps, AAC 160 kbps
#           (Opus for WHIP), sources color,media,text. Override with --profile/--collection/--scene, --width
#           --height --fps, --video-bitrate --audio-bitrate, --encoder x264|apple_h264|nvenc|..., --sources
#           color,media,text,clock,browser:URL,image:PATH, --media-file clip.mp4, --text "label";
#           --stop-first if OBS is already streaming; --bitrate to wait for SRS's 30 s kbps window.
```

What it does, in order (each step is also available as a single `obsws.py call`):

1. `GetVersion`, refuse to reconfigure while streaming unless `--stop-first`.
2. Profile: `CreateProfile` (clean defaults, switches to it) or `SetCurrentProfile`; `SetVideoSettings`
   (canvas/output/fps); `SetProfileParameter` for `Output/Mode=Simple`, `SimpleOutput/VBitrate`,
   `ABitrate`, `StreamEncoder`, `StreamAudioEncoder` (opus for WHIP, aac otherwise), `Preset`.
3. `SetStreamServiceSettings` (`rtmp_custom` for RTMP/SRT, `whip_custom` for WHIP), **then** re-activate the
   profile by switching to another one and back. Activation is when OBS rebuilds the output handler
   (mode, encoder ids, service binding) and re-derives simple-mode encoder defaults from the selected
   service's codecs, so service first, reload second; otherwise a previously selected WHIP service drags
   the audio encoder back to Opus.
4. Scene collection: `CreateSceneCollection` or `SetCurrentSceneCollection` (both block until loaded).
5. Scene: `CreateScene` + `SetCurrentProgramScene`.
6. Sources: `CreateInput`/`SetInputSettings` + `SetSceneItemTransform`: a canvas-sized colour background,
   a looping ffmpeg-generated test clip with a 440 Hz tone (real motion and audio, so bitrate numbers mean
   something), and a text label; optionally a browser clock or an image.
7. `--start`: `StartStream` and wait for `outputActive`; `--verify`: poll the SRS HTTP API
   (`/api/v1/streams/`) until `publish.active` and print codec, resolution, kbps and play URLs. SRS's
   `kbps.recv_30s` is a trailing 30 s average and reads 0 right after start: add `--bitrate` (waits for it)
   or re-run `verify` later when the report needs a bitrate figure.

Then observe and steer:

```bash
python3 scripts/obs_srs_test.py stream status|start|stop
python3 scripts/obs_srs_test.py verify --srs-api http://localhost:1985 --app live --stream livestream
python3 scripts/obsws.py screenshot --out /tmp/program.png --width 640 --height 360   # see what is streamed
python3 scripts/obsws.py events --types StreamStateChanged --timeout 60                 # reconnect behaviour
python3 scripts/obsws.py call GetStats
python3 scripts/obs_srs_test.py teardown        # remove the test profile + collection when done
```

For anything the recipe does not cover, call requests directly. Names are exact and case-sensitive; input
names are global (not per scene); most requests accept `...Name` or `...Uuid`:

```bash
python3 scripts/obsws.py call GetInputKindList
python3 scripts/obsws.py call CreateInput '{"sceneName":"SRS Scene","inputName":"Cam","inputKind":"macos-avcapture","inputSettings":{}}'
python3 scripts/obsws.py call SetInputSettings '{"inputName":"SRS Label","inputSettings":{"text":"take 2"}}'
python3 scripts/obsws.py call SetStreamServiceSettings '{"streamServiceType":"rtmp_custom","streamServiceSettings":{"server":"rtmp://srs:1935/live","key":"k?secret=x","use_auth":false}}'
python3 scripts/obsws.py batch '[{"requestType":"StartStream"},{"requestType":"Sleep","requestData":{"sleepMillis":5000}},{"requestType":"GetStreamStatus"},{"requestType":"StopStream"}]'
```

The response is printed as JSON; a refused request prints `requestStatus` (`code`, `comment`) and exits 1.
Look the code up in `references/websocket-protocol.md` and fix the cause rather than retrying blindly.

## Step 3: Report

Report what OBS did and what SRS saw, separately, because a "stream active" in OBS with nothing in SRS is
the most common failure and the two sides disagree for a reason (wrong URL, blocked port, unreachable ICE
candidate, SRS feature disabled). Include: OBS version and obs-websocket version, profile/collection/scene
used, target URL, encoder and bitrate, `GetStreamStatus` (bytes, frames, congestion, reconnecting), the
SRS API view (`publish.active`, codecs, resolution, kbps), and the newest OBS log path when something
failed. Mention any dialog you had to work around and any state you changed on the user's OBS (enabled
websocket, restarted OBS, created/removed profiles or collections).

If the SRS side shows something wrong (missing audio track, wrong codec, no HLS), report it as an SRS
finding for the user to review; do not change SRS configuration or code to make the test pass unless asked.

## Reference material (load as needed)

- `references/websocket-protocol.md` - handshake/auth, message envelopes, status codes, every request the
  automation relies on with exact field names, events, and behaviour learned from the source (queued
  profile creation, no RemoveSceneCollection, service merge semantics). Read when writing raw `call`s or
  when a request fails.
- `references/config-files.md` - where the config dir is per OS, `user.ini`/`global.ini`, `basic.ini` keys
  for output/video/audio, `service.json` variants (RTMP/SRT/WHIP/known services), `streamEncoder.json`,
  scene collection JSON structure, obs-websocket `config.json`, crash sentinel, logs, and an offline
  pre-seeding template. Read when OBS is not running or a setting does not stick.
- `references/sources-and-encoders.md` - input kinds per platform, settings for colour/text/media/browser/
  image/capture sources, audio routing, scene item transforms, encoder ids and keys, and how to pick a
  composition for bitrate, latency, reconnect and codec tests. Read before adding sources beyond the recipe.
- `references/srs-targets.md` - RTMP/SRT/WHIP URL formats for SRS, required SRS config, verification via
  the SRS API and playback URLs, Docker candidate/port caveats, and a symptom -> cause table. Read when the
  destination is SRS and the stream does not show up.
- `references/cli-and-lifecycle.md` - binary locations, all command line flags, startup dialogs that block
  automation, graceful stop per OS, headless/CI notes, version differences (OBS 27 vs 28+, 31+, 32.x).
  Read before launching OBS on a new machine or debugging a launch that hangs.

## Gotchas that cost time

- **Nothing answers**: OBS is showing a modal dialog. Websocket requests that touch the UI thread wait
  behind it. Check the newest log, clear sentinels, use the launch flags, stop outputs before quitting.
- **Settings did not apply**: `SetProfileParameter` writes `basic.ini` but output mode and encoder ids are
  read at profile activation; switch profiles (service first) or restart. Bitrate, preset and video settings
  apply at the next stream start (`SetVideoSettings` applies immediately, but is refused while any output
  is active). Activation also rewrites `StreamAudioEncoder` to a codec the selected service supports.
- **Removing the current profile** (`RemoveProfile`) opens a confirmation dialog that blocks OBS; switch to
  another profile first. `teardown` does this.
- **WHIP fails after ~40 s** with `PeerConnection state: Failed` and SRS `DTLS: Hang`: the ICE candidate SRS
  advertised (`RTC: Use candidates <ip>` in the SRS log; the container's 172.17.x.x when `CANDIDATE` is
  unset) is unreachable. Add `?eip=<ip OBS can reach>` or set `CANDIDATE` for SRS. WHIP needs Opus audio
  (`SimpleOutput/StreamAudioEncoder=opus`), and OBS's WHIP output stops on failure instead of reconnecting
  (`outputReconnecting` stays false, no `RECONNECTING` events), unlike RTMP/SRT.
- **SRT**: put `streamid=#!::r=app/stream,m=publish` in the URL and leave `key` empty (it is the passphrase).
  SRS needs `srt_server` enabled and `srt_to_rtmp on` for the stream to appear in the API.
- **Static scenes** encode to a few kbps; the media test clip exists so bitrate tests are meaningful.
- **Input names** are global. A second `CreateInput` with the same name fails with 601; reuse via
  `CreateSceneItem` or pick unique names per test. Parallel tests go in separate instances, never in one OBS.
- **Scene collections cannot be removed over the websocket**; `teardown` switches away and deletes the JSON.
- **Do not edit files while OBS runs** (except `plugin_config/obs-websocket/config.json`, which is read once
  at startup and overwritten only when the GUI settings dialog is saved).
- **Safe Mode disables the websocket**; never launch with `--safe-mode` or pick it in the crash dialog.
