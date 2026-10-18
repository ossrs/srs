# Publishing from OBS to SRS: URLs, service types, verification

SRS (Simple Realtime Server) accepts OBS over three protocols. All three end up as the same stream
`app/stream` inside SRS, so playback and API checks are identical.

## Contents

- [Destination table](#destination-table)
- [Start the local SRS build](#start-the-local-srs-build)
- [RTMP](#rtmp)
- [SRT](#srt)
- [WHIP (WebRTC)](#whip-webrtc)
- [Verifying on the SRS side](#verifying-on-the-srs-side)
- [Playback URLs for a second check](#playback-urls-for-a-second-check)
- [Automated tests](#automated-tests)
- [Typical failures](#typical-failures)

## Destination table

| protocol | OBS `streamServiceType` | `streamServiceSettings` | SRS listener (defaults) |
| --- | --- | --- | --- |
| RTMP | `rtmp_custom` | `{"server":"rtmp://HOST/live","key":"livestream","use_auth":false}` | TCP 1935 (`listen 1935;`) |
| SRT | `rtmp_custom` | `{"server":"srt://HOST:10080","key":"#!::r=live/livestream,m=publish","use_auth":false}` | UDP 10080 (`srt_server { listen 10080; }`, `vhost { srt { enabled on; srt_to_rtmp on; } }`) |
| WHIP | `whip_custom` | `{"server":"http://HOST:1985/rtc/v1/whip/?app=live&stream=livestream","bearer_token":""}` | HTTP API 1985 + UDP 8000 (`rtc_server { listen 8000; candidate ...; }`, `vhost { rtc { enabled on; rtmp_to_rtc on; rtc_to_rtmp on; } }`) |

`obs_srs_test.py setup --rtmp|--srt|--whip ...` fills these in; `obsws.py call SetStreamServiceSettings
'{...}'` does it by hand. Change the service only while the stream is stopped (OBS refuses otherwise).

## Start the local SRS build

Use the checkout's `trunk/objs/srs`, built with SRT enabled (`./configure --srt=on` and `make`
from `trunk/`). Run it from `trunk/` with environment-only configuration (`-e`), without a config file:

```bash
env SRS_RTMP_LISTEN=15100 SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=15101 \
    SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN=15102 \
    SRS_VHOST_HTTP_REMUX_ENABLED=on SRS_VHOST_HLS_ENABLED=on \
    SRS_SRT_SERVER_ENABLED=on SRS_SRT_SERVER_LISTEN=15103 \
    SRS_VHOST_SRT_ENABLED=on SRS_VHOST_SRT_SRT_TO_RTMP=on \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN=15104 \
    SRS_RTC_SERVER_CANDIDATE=127.0.0.1 SRS_VHOST_RTC_ENABLED=on \
    SRS_VHOST_RTC_RTMP_TO_RTC=on SRS_VHOST_RTC_RTC_TO_RTMP=on \
    ./objs/srs -e
```

- **Ports:** this example uses a test slot. The destination table above lists conventional defaults;
  replace its ports with the listeners above when using this command.
- **Verification:** pass `--srs-api http://127.0.0.1:15101 --http-url http://127.0.0.1:15102`.
- **WHIP:** the loopback candidate suits OBS on the same host; use a reachable candidate or `--eip`
  when OBS is on another host.
- **Logs:** `-e` logs to the console; redirect stdout/stderr to your test log if needed.
- **Source:** environment variable names and defaults are in `trunk/src/app/srs_app_config.cpp`.

## RTMP

- `server` is `rtmp://host[:port]/app` and `key` is the stream name; OBS joins them as the tcUrl + stream.
  For SRS the whole URL may also be written as `rtmp://host/app/stream` in `--rtmp`; the script splits it.
- Query parameters for SRS auth or vhost go in the key: `livestream?secret=xxx` or `livestream?vhost=my.vhost`.
  Both `--rtmp 'rtmp://host/live/livestream?x=y'` and `--rtmp-server rtmp://host/live --rtmp-key 'livestream?x=y'`
  preserve them; verification strips the query from the SRS stream name.
- SRS logs `RTMP client ip=..., fd=...` then `client identified, type=fmle-publish, vhost=__defaultVhost__, app=live, stream=livestream`.
- OBS log: `[rtmp stream: 'simple_stream'] Connecting to RTMP URL rtmp://host/live...` then
  `Connection to rtmp://host/live (ip) successful`.

## SRT

- SRS uses the SRT streamid `#!::r=app/stream,m=publish` (`m=request` for play).
  Set `--srt srt://host:10080 --srt-streamid '#!::r=app/stream,m=publish'`; the script writes the
  streamid to the service `key`, and parses app/stream from it for verification.
- OBS detects `srt://` and uses the FFmpeg MPEG-TS muxer output (`ffmpeg_mpegts_muxer`); x264/AAC are fine.
  OBS maps `key` to the streamid and `password` to the encryption passphrase when `use_auth` is true
  (`plugins/rtmp-services/rtmp-custom.c:149–163`). `--srt-passphrase <passphrase>` fills these password
  fields; leave it unset unless SRS `srt_server { passphrase ...; }` is configured.
- A `streamid=` in the URL overrides the key in OBS (`plugins/obs-ffmpeg/obs-ffmpeg-srt.h:769–775`).
  The script accepts literal `#!::` and URL-encoded forms, moves that value into `key`, and removes
  only the streamid option. Other options remain, e.g. `srt://host:10080?latency=200&mode=caller`.
- SRS must be built with SRT (`--srt=on`, the default in release builds) and `srt_to_rtmp on;` for the
  stream to show up over RTMP/HTTP-FLV/HLS and in the API. Check the SRS log for
  `srt: accept ... streamid=#!::r=live/livestream,m=publish` and `SRT: srt to rtmp`.
- Enable `SRS_SRT_SERVER_ENABLED=on` and `SRS_VHOST_SRT_ENABLED=on`; use
  `SRS_VHOST_SRT_SRT_TO_RTMP=on` for playback conversion. OBS must reach the SRT UDP listener.
- **Keep AAC for SRT.** OBS picks the audio encoder when the profile is activated, from the service set
  at that moment.
  - With the SRT service set, OBS keeps AAC.
  - With a WHIP service still set from an earlier run, OBS picks Opus for the SRT stream.
  - SRS drops Opus in MPEG-TS (`ts: drop pid=0x101, stream=0x6` in its log), so the stream is video only.
  - The recipe sets the service before the profile reload, so it keeps AAC.

## WHIP (WebRTC)

- Endpoint `http://host:1985/rtc/v1/whip/?app=live&stream=livestream` (HTTPS via 1990 when configured).
  OBS uses `whip_output` (libdatachannel): H264 with `bf=0` (the service forces it) and **Opus** audio.
  Simple output mode: set `SimpleOutput/StreamAudioEncoder=opus` (the script does). AAC audio fails.
- SRS must have `rtc_server { enabled on; listen 8000; candidate $CANDIDATE; }` and the vhost `rtc { enabled on; }`.
  SRS converts to RTMP (`rtc_to_rtmp on`) so the stream appears in the API with codec H264/AAC.
- The ICE candidate SRS advertises comes from `candidate` (`*` = first non-loopback interface IP, or the
  `SRS_RTC_SERVER_CANDIDATE` in environment-only mode). The client can override per request with `?eip=<ip>`; `obs_srs_test.py` adds
  `eip=127.0.0.1` automatically for localhost targets (`--eip` for others, `--no-eip` to disable).
- OBS log: `[obs-webrtc] [whip_output: 'simple_stream'] PeerConnection state is now: Connecting` then
  `Connected`. `Failed` after ~40 s plus `DTLS: Hang, done=0` in the SRS log = unreachable candidate/UDP
  port. HTTP 4xx on the POST = wrong URL, app/stream missing, or SRS `rtc` disabled.
- **`DTLS: Hang` at quit is harmless.** SRS also logs `DTLS: Hang, done=0` when a WHIP session that
  logged `RTC: DTLS handshake done` ends because OBS quits. Only a hang without that earlier line means
  an unreachable candidate.
- **The bridged stream starts at the second keyframe.** With `rtc_to_rtmp on`:
  - SRS discards RTP until the first RTCP sender report, about 1 s in (`RTC: Discard no-sync`), so the
    first keyframe is lost.
  - The bridge asks for a keyframe (`RTC: to rtmp bridge request key frame`, then a PLI); OBS does not
    send one early.
  - The bridge starts at the next scheduled keyframe: about 8.3 s with OBS x264 defaults (keyint 250
    at 30 fps). Until then HTTP-FLV, HLS and the API resolution are empty.
  - So wait up to 15 s for the WHIP stream's resolution, not the 8 s that RTMP and SRT need.
- **The API shows the bridged stream.** The stream entry first reads Opus and 0×0 from the RTC source,
  then AAC and the real resolution from the bridge.
- **WHIP frame counts are not reliable.** SRS adds a WHIP publisher's frame count to the statistics
  only past 288 frames, so `frames` reads 0 for about 10 s. At 14 s the API read `frames: 289`,
  `audio_frames: 289` and `video_frames: 0`. Check only that `frames` is non-zero, and treat 0 as a
  known SRS defect.
- **Bearer token**: SRS supports Bearer authentication for WHIP and WHEP signaling. It is off by default.
  - **Turn it on in SRS** with `SRS_HTTP_API_AUTH_ENABLED=on`, `SRS_HTTP_API_AUTH_TYPE=bearer`,
    `SRS_HTTP_API_AUTH_TOKEN=<token>` and `SRS_HTTP_API_AUTH_RTC_BEARER_ENABLED=on`. Without the last
    one, the token guards only `/api/`, not `/rtc/` (`SrsHttpBearerAuthenticator` in
    `trunk/src/protocol/srs_protocol_http_stack.cpp`).
  - **Set it in OBS** as the WHIP service's `bearer_token` (`obs_srs_test.py --bearer-token <token>`).
    OBS sends it as `Authorization: Bearer <token>` on the WHIP POST (`plugins/obs-webrtc/whip-output.cpp`).
  - **A wrong or missing token** gets HTTP 401 on the POST, and OBS does not go live.
  - **The API needs the token too**: with Bearer auth on, `/api/v1/streams/` also returns 401 without it.
  - **Without Bearer auth**, leave `bearer_token` empty. SRS can still authorize WHIP publishing with the
    `on_publish` HTTP callback, or with both together.

## Verifying on the SRS side

HTTP API on port 1985 (`http_api { enabled on; listen 1985; }`):

```
GET /api/v1/versions            -> {"code":0,"data":{"version":"8.0.60",...},...}
GET /api/v1/streams/            -> {"streams":[{"app":"live","name":"livestream","publish":{"active":true,"cid":"..."},
                                     "clients":1,"video":{"codec":"H264","profile":"High","width":1280,"height":720},
                                     "audio":{"codec":"AAC","sample_rate":44100,"channel":2},
                                     "kbps":{"recv_30s":2107,"send_30s":0},"frames":..., "live_ms":...}]}
GET /api/v1/clients/            -> publisher and players with type, ip, url, alive seconds
GET /api/v1/summaries           -> server load, ok, now_ms
DELETE /api/v1/clients/<id>     -> kick a client (tests reconnect logic)
```

Rules of thumb: `publish.active=true` within ~2 s of `StartStream` for RTMP, ~1 s for WHIP, ~3 s for SRT.
WHIP's codecs and resolution come from the RTMP bridge, up to ~9 s later ([WHIP](#whip-webrtc)).
`kbps.recv_30s` needs 30 s of traffic to be meaningful. `video`/`audio` appear after the first
sequence headers. A stream that flaps between present and absent means OBS is reconnecting: look at
`GetStreamStatus.outputReconnecting` and the OBS log.

`obs_srs_test.py verify --srs-api http://host:1985 --app live --stream livestream` polls this and prints
codec, resolution, kbps and play URLs. Pass `--srs-api` for a non-default API port and `--http-url`
for a non-default HTTP playback port (otherwise the API host on port 8080 is assumed).
Exit code 6 when not publishing.

## Playback URLs for a second check

Given `http_server { listen 8080; }` and the vhost defaults:

- RTMP `rtmp://host/live/livestream` (`ffplay -i rtmp://host/live/livestream`)
- HTTP-FLV `http://host:8080/live/livestream.flv` (enable `SRS_VHOST_HTTP_REMUX_ENABLED=on`)
- HLS `http://host:8080/live/livestream.m3u8` (needs `hls { enabled on; }`)
- WebRTC WHEP `http://host:1985/rtc/v1/whep/?app=live&stream=livestream`, player page `http://host:8080/players/whep.html`
- SRT `srt://host:10080?streamid=#!::r=live/livestream,m=request`

`ffprobe -v error -show_streams -i http://host:8080/live/livestream.flv` is a cheap headless check of
codec, resolution and frame rate after publishing.

## Automated tests

The `srs-develop` OBS test scripts (see [Testing SRS](../SKILL.md#testing-srs)) run one isolated
instance per case. What keeps them fast and stable:

- **Small encodes**: 640×360 at 30 fps and 800 kbps, so several instances encode at once without
  saturating the CPU.
- **One shared clip**: the harness makes a four-second looping 640×360 H.264/AAC clip once, before
  launching OBS, so no case waits for ffmpeg.
- **Staggered starts**: `obs-lane-test.sh` starts the cases 2 s apart. Fresh instances that reset
  video at the same moment can hang OBS in `SetVideoSettings`, inside `obs_reset_video`.
- **One relaunch**: the harness bounds the setup recipe at 20 s. When OBS stops answering, or exits
  right after the first `CreateProfile` (seen on macOS), it force-quits the instance, relaunches it
  and reruns the idempotent setup once.
- **Case times**: about 5–7 s for RTMP, 4–6 s for SRT and 14 s for WHIP, which waits for the second
  keyframe. The three in parallel take about 18 s.

SRS outside this machine's process list:

- **SRS in Docker**: publish the TCP ports and the UDP ports (RTC, SRT) on `127.0.0.1`; WHIP keeps
  `eip=127.0.0.1`.
- **SRS in WSL** for OBS on Windows: in NAT mode, TCP reaches SRS on `127.0.0.1`, but UDP reaches it
  only on the WSL IP (`wsl hostname -I`, new each boot). Publish both RTMP and WHIP to that IP, with
  `--eip <WSL IP>` for WHIP.

## Typical failures

| symptom | cause / fix |
| --- | --- |
| OBS `StartStream` succeeds, status flips back to inactive, log `Connection to rtmp://... failed` | wrong host/port, SRS not listening, firewall |
| RTMP connects then disconnects within seconds, SRS log `stream ... already publishing` | another publisher holds the same stream; stop it or change the key |
| WHIP `PeerConnection state: Failed`, SRS `DTLS: Hang` | unreachable ICE candidate; pass `--eip` or set `SRS_RTC_SERVER_CANDIDATE`; allow the RTC UDP listener |
| WHIP HTTP 400/404 | `rtc` disabled in vhost, bad `app`/`stream`, or wrong API port |
| SRT never appears in API | SRS built without SRT or `srt_to_rtmp off`; SRT UDP listener unreachable; `streamid` typo (`#!::r=`) |
| SRS shows `video` but no `audio` | OBS has no audio source producing samples (media source muted, mic missing); WHIP with AAC selected |
| SRT stream is video only, SRS log `ts: drop pid=0x101, stream=0x6` | OBS encodes Opus because a WHIP service was set when the profile was activated; set the SRT service, then reload the profile |
| WHIP is active in the API but resolution, HTTP-FLV and HLS stay empty for ~8 s | the bridge waits for the second keyframe; wait up to 15 s, or set a shorter keyframe interval in OBS |
| `setup` hangs at `SetVideoSettings` with several new instances | OBS hangs in `obs_reset_video` under GPU contention; stagger the launches, force-quit and relaunch the instance |
| `SetStreamServiceSettings` -> 500 OutputRunning | stop the stream first (`obs_srs_test.py stream stop`) |
| bitrate in SRS far below `VBitrate` | static scene (nothing moves); add the media test clip or a browser clock |
