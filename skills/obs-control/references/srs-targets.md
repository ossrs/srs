# Publishing from OBS to SRS: URLs, service types, verification

SRS (Simple Realtime Server) accepts OBS over three protocols. All three end up as the same stream
`app/stream` inside SRS, so playback and API checks are identical.

## Contents

- [Destination table](#destination-table)
- [RTMP](#rtmp)
- [SRT](#srt)
- [WHIP (WebRTC)](#whip-webrtc)
- [Verifying on the SRS side](#verifying-on-the-srs-side)
- [Playback URLs for a second check](#playback-urls-for-a-second-check)
- [SRS in Docker](#srs-in-docker)
- [Typical failures](#typical-failures)

## Destination table

| protocol | OBS `streamServiceType` | `streamServiceSettings` | SRS listener (defaults) |
| --- | --- | --- | --- |
| RTMP | `rtmp_custom` | `{"server":"rtmp://HOST/live","key":"livestream","use_auth":false}` | TCP 1935 (`listen 1935;`) |
| SRT | `rtmp_custom` | `{"server":"srt://HOST:10080?streamid=#!::r=live/livestream,m=publish","key":""}` | UDP 10080 (`srt_server { listen 10080; }`, `vhost { srt { enabled on; srt_to_rtmp on; } }`) |
| WHIP | `whip_custom` | `{"server":"http://HOST:1985/rtc/v1/whip/?app=live&stream=livestream","bearer_token":""}` | HTTP API 1985 + UDP 8000 (`rtc_server { listen 8000; candidate ...; }`, `vhost { rtc { enabled on; rtmp_to_rtc on; rtc_to_rtmp on; } }`) |

`obs_srs_test.py setup --rtmp|--srt|--whip ...` fills these in; `obsws.py call SetStreamServiceSettings
'{...}'` does it by hand. Change the service only while the stream is stopped (OBS refuses otherwise).

## RTMP

- `server` is `rtmp://host[:port]/app` and `key` is the stream name; OBS joins them as the tcUrl + stream.
  For SRS the whole URL may also be written as `rtmp://host/app/stream` in `--rtmp`; the script splits it.
- Query parameters for SRS auth or vhost go in the key: `livestream?secret=xxx` or `livestream?vhost=my.vhost`.
- SRS logs `RTMP client ip=..., fd=...` then `client identified, type=fmle-publish, vhost=__defaultVhost__, app=live, stream=livestream`.
- OBS log: `[rtmp stream: 'simple_stream'] Connecting to RTMP URL rtmp://host/live...` then
  `Connection to rtmp://host/live (ip) successful`.

## SRT

- SRS uses the `streamid` query in the URL-like form `#!::r=app/stream,m=publish` (`m=request` for play).
  URL-encoding is not needed in `service.json`; keep the `#!::` literal.
- OBS detects `srt://` and uses the FFmpeg MPEG-TS muxer output (`ffmpeg_mpegts_muxer`); x264/AAC are fine.
  `key` becomes the SRT passphrase (leave empty unless SRS `srt_server { passphrase ...; }` is set).
  Latency options can be appended: `srt://host:10080?streamid=...&latency=200&mode=caller`.
- SRS must be built with SRT (`--srt=on`, the default in release builds) and `srt_to_rtmp on;` for the
  stream to show up over RTMP/HTTP-FLV/HLS and in the API. Check the SRS log for
  `srt: accept ... streamid=#!::r=live/livestream,m=publish` and `SRT: srt to rtmp`.
- If SRS runs in Docker, UDP 10080 must be published (`-p 10080:10080/udp`); the default `ossrs/srs`
  docker command uses `conf/docker.conf` which has SRT disabled unless started with `conf/srt.conf`.

## WHIP (WebRTC)

- Endpoint `http://host:1985/rtc/v1/whip/?app=live&stream=livestream` (HTTPS via 1990 when configured).
  OBS uses `whip_output` (libdatachannel): H264 with `bf=0` (the service forces it) and **Opus** audio.
  Simple output mode: set `SimpleOutput/StreamAudioEncoder=opus` (the script does). AAC audio fails.
- SRS must have `rtc_server { enabled on; listen 8000; candidate $CANDIDATE; }` and the vhost `rtc { enabled on; }`.
  SRS converts to RTMP (`rtc_to_rtmp on`) so the stream appears in the API with codec H264/AAC.
- The ICE candidate SRS advertises comes from `candidate` (`*` = first non-loopback interface IP, or the
  `CANDIDATE` env). The client can override per request with `?eip=<ip>`; `obs_srs_test.py` adds
  `eip=127.0.0.1` automatically for localhost targets (`--eip` for others, `--no-eip` to disable).
- OBS log: `[obs-webrtc] [whip_output: 'simple_stream'] PeerConnection state is now: Connecting` then
  `Connected`. `Failed` after ~40 s plus `DTLS: Hang, done=0` in the SRS log = unreachable candidate/UDP
  port. HTTP 4xx on the POST = wrong URL, app/stream missing, or SRS `rtc` disabled.
- Bearer token: SRS ignores it unless a hook/proxy checks it; leave `bearer_token` empty.

## Verifying on the SRS side

HTTP API on port 1985 (`http_api { enabled on; listen 1985; }`):

```
GET /api/v1/versions            -> {"code":0,"data":{"version":"6.0.191"}}
GET /api/v1/streams/            -> {"streams":[{"app":"live","name":"livestream","publish":{"active":true,"cid":"..."},
                                     "clients":1,"video":{"codec":"H264","profile":"High","width":1280,"height":720},
                                     "audio":{"codec":"AAC","sample_rate":44100,"channel":2},
                                     "kbps":{"recv_30s":2107,"send_30s":0},"frames":..., "live_ms":...}]}
GET /api/v1/clients/            -> publisher and players with type, ip, url, alive seconds
GET /api/v1/summaries           -> server load, ok, now_ms
DELETE /api/v1/clients/<id>     -> kick a client (tests reconnect logic)
```

Rules of thumb: `publish.active=true` within ~2 s of `StartStream` for RTMP, ~1 s for WHIP, ~3 s for SRT.
`kbps.recv_30s` needs 30 s of traffic to be meaningful. `video`/`audio` appear after the first
sequence headers. A stream that flaps between present and absent means OBS is reconnecting: look at
`GetStreamStatus.outputReconnecting` and the OBS log.

`obs_srs_test.py verify --srs-api http://host:1985 --app live --stream livestream` polls this and prints
codec, resolution, kbps and play URLs. Exit code 1 when not publishing.

## Playback URLs for a second check

Given `http_server { listen 8080; }` and the vhost defaults:

- RTMP `rtmp://host/live/livestream` (`ffplay -i rtmp://host/live/livestream`)
- HTTP-FLV `http://host:8080/live/livestream.flv` (needs `http_remux { enabled on; }`, on in `conf/docker.conf`)
- HLS `http://host:8080/live/livestream.m3u8` (needs `hls { enabled on; }`)
- WebRTC WHEP `http://host:1985/rtc/v1/whep/?app=live&stream=livestream`, player page `http://host:8080/players/whep.html`
- SRT `srt://host:10080?streamid=#!::r=live/livestream,m=request`

`ffprobe -v error -show_streams -i http://host:8080/live/livestream.flv` is a cheap headless check of
codec, resolution and frame rate after publishing.

## SRS in Docker

`docker run --rm -p 1935:1935 -p 1985:1985 -p 8080:8080 -p 8000:8000/udp -p 10080:10080/udp
-e CANDIDATE=<host LAN ip> ossrs/srs:6 ./objs/srs -c conf/srt.conf` (or `conf/rtc.conf`, `conf/docker.conf`).
`conf/docker.conf` has `candidate $CANDIDATE;`; without the env var SRS logs `Best matched ip=172.17.0.2,
ifname=eth0` and `RTC: Use candidates 172.17.0.2`, an address the host cannot reach, so WHIP/WHEP need
`?eip=<reachable ip>` on the client side (`RTC: Use candidates 127.0.0.1, 172.17.0.2` once it works, then
`session STUN done` and `DTLS handshake done`). `docker logs srs --tail 50` is the SRS log (UTC timestamps).
The API host inside URLs is the Docker host, not the container. OBS's WHIP output does not retry after
`PeerConnection state: Failed`; restart the stream after fixing the candidate.

## Typical failures

| symptom | cause / fix |
| --- | --- |
| OBS `StartStream` succeeds, status flips back to inactive, log `Connection to rtmp://... failed` | wrong host/port, SRS not listening, firewall |
| RTMP connects then disconnects within seconds, SRS log `stream ... already publishing` | another publisher holds the same stream; stop it or change the key |
| WHIP `PeerConnection state: Failed`, SRS `DTLS: Hang` | unreachable ICE candidate; pass `--eip`/set `CANDIDATE`, publish UDP 8000 |
| WHIP HTTP 400/404 | `rtc` disabled in vhost, bad `app`/`stream`, or wrong API port |
| SRT never appears in API | SRS built without SRT or `srt_to_rtmp off`; UDP 10080 not published; `streamid` typo (`#!::r=`) |
| SRS shows `video` but no `audio` | OBS has no audio source producing samples (media source muted, mic missing); WHIP with AAC selected |
| `SetStreamServiceSettings` -> 500 OutputRunning | stop the stream first (`obs_srs_test.py stream stop`) |
| bitrate in SRS far below `VBitrate` | static scene (nothing moves); add the media test clip or a browser clock |
