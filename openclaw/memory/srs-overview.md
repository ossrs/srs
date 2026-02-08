# SRS Overview

## What is SRS

SRS is a **simple, high-efficiency, real-time media server**. It receives streams from publishers and delivers them to players.

## How SRS Works With Tools

```
┌─────────────────────────────────────────────────────────────────┐
│                         PUBLISHERS                              │
│   FFmpeg, OBS, Larix, vMix, hardware encoders, browsers, apps   │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
                        ┌───────────┐
                        │    SRS    │
                        └───────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────----───┐
│                          PLAYERS                                    │
│ FFmpeg, VLC, ffplay, ExoPlayer, IJKPlayer, browsers, hardware, apps │
└────────────────────────────────────────────────────────────────----─┘
```

### Publishers

- **FFmpeg** — Command-line tool for encoding/transcoding. Pushes RTMP, SRT, WHIP (WebRTC), etc.
- **OBS (Open Broadcaster Software)** — Popular open-source streaming app. Pushes RTMP, SRT, WHIP (WebRTC).
- **Larix Broadcaster** — Mobile streaming app (iOS/Android). Pushes RTMP, SRT, WHIP (WebRTC).
- **vMix** — Windows live production software. Pushes RTMP, SRT.
- **Hardware encoders** — Devices like Teradek, Haivision, Blackmagic. Push RTMP, SRT.
- **Browsers** — Via WHIP (WebRTC).
- **Apps** — Custom apps using RTMP/SRT/WebRTC SDKs.

### Players

- **VLC** — Cross-platform media player. Plays RTMP, SRT, HLS, HTTP-FLV, RTSP.
- **FFmpeg** — Command-line tool. Plays RTMP, SRT, HLS, HTTP-FLV (all protocols except WHEP).
- **ffplay** — FFmpeg's built-in player. Same protocol support as FFmpeg (all except WHEP).
- **ExoPlayer** — Android media player library. Plays HLS, DASH.
- **IJKPlayer** — Cross-platform player based on FFmpeg (by Bilibili). Plays RTMP, HLS, HTTP-FLV.
- **mpegts.js (formerly flv.js)** — Browser JavaScript player. Plays HTTP-FLV, HTTP-TS, HLS via MSE.
- **Browsers** — Plays HTTP-FLV, HLS, HTTP-TS via MSE, and WHEP (WebRTC).
- **Hardware decoders** — Set-top boxes, smart TVs, etc. Play HLS, RTMP.
- **Apps** — Custom apps using player SDKs.

## Protocols (Each Supports Input AND Output)

- **RTMP** — Publishers: OBS, FFmpeg, Larix. Players: VLC, ffplay. Traditional live streaming.
- **SRT** — Publishers: OBS, vMix, hardware. Players: ffplay, VLC, hardware. Long-distance, professional broadcast.
- **WebRTC** — Publishers: Browsers, apps. Players: Browsers, apps. Real-time communication, conferences.
- **HLS/HTTP-FLV** — Players only: ExoPlayer, mpegts.js, browsers. Wide compatibility playback.
- **RTSP** — Players only: VLC, FFmpeg, ffplay. Surveillance, IP cameras.

## Protocol Transmux (Converting Between Protocols)

SRS converts directly between protocols.

- **WebRTC to RTMP** — `rtc_to_rtmp on` in vhost config. Transcodes Opus to AAC audio.
- **RTMP to WebRTC** — `rtmp_to_rtc on` in vhost config. Transcodes AAC to Opus audio.
- **SRT to RTMP** — `srt_to_rtmp on` in vhost config. SRT uses MPEG-TS, demuxed to RTMP.
- **SRT to WebRTC** — Converts directly. Transcodes AAC to Opus audio.
- **GB28181 to RTMP** — For surveillance cameras pushing PS streams. Depends on external [srs-sip](https://github.com/ossrs/srs-sip) for SIP signaling.
- **RTMP to HLS** — Segments into .m3u8 + .ts files. 3–5s latency.
- **RTMP to HTTP-FLV** — Transmux to FLV over HTTP. ~1s latency.
- **RTMP to HTTP-TS** — Transmux to MPEG-TS over HTTP.
- **RTMP to RTSP** — `rtmp_to_rtsp on` in vhost config. TCP transport only.
- **RTMP to MPEG-DASH** — Segments into DASH manifest + segments.

## Tools

- **FFmpeg** — https://ffmpeg.org
- **OBS** — https://obsproject.com
- **Larix Broadcaster** — https://softvelum.com/larix/
- **vMix** — https://www.vmix.com
- **VLC** — https://www.videolan.org/vlc/
- **ffplay** — https://ffmpeg.org/ffplay.html
- **ExoPlayer** — https://github.com/androidx/media
- **IJKPlayer** — https://github.com/bilibili/ijkplayer
- **mpegts.js (formerly flv.js)** — https://github.com/xqq/mpegts.js
