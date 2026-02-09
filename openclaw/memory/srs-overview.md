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

### Tools

- **FFmpeg** — https://ffmpeg.org
- **OBS** — https://obsproject.com
- **Larix Broadcaster** — https://softvelum.com/larix/
- **vMix** — https://www.vmix.com
- **VLC** — https://www.videolan.org/vlc/
- **ffplay** — https://ffmpeg.org/ffplay.html
- **ExoPlayer** — https://github.com/androidx/media
- **IJKPlayer** — https://github.com/bilibili/ijkplayer
- **mpegts.js (formerly flv.js)** — https://github.com/xqq/mpegts.js

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

## Codecs

### Video Codecs

- **H.264/AVC** — Core video codec, supported since v0.2 (2013). Works across all protocols: RTMP, HLS, HTTP-FLV, HTTP-TS, SRT, WebRTC, MPEG-DASH, GB28181, DVR.
- **H.265/HEVC** — Supported since v6.0 via Enhanced RTMP. Works across RTMP, HTTP-FLV, HTTP-TS, HLS (including fMP4/LLHLS in v7.0), MPEG-DASH, SRT, GB28181, DVR (MP4/FLV). WebRTC HEVC supported in v7.0 (RTMP↔WebRTC conversion, Safari playback).
- **AV1** — [Experimental] WebRTC only, v4.0.207+.
- **VP9** — WebRTC-to-WebRTC streaming only, v7.0.123+.

### Audio Codecs

- **AAC** — Core audio codec, supported since v1.0. Works across all protocols: RTMP, HLS, HTTP-FLV, HTTP-TS, SRT, MPEG-DASH, GB28181, DVR. Transcoded to Opus for WebRTC output.
- **MP3** — Supported for HLS (H.264+MP3), HTTP-FLV/TS, DVR. v1.0+. Transcoded to Opus for WebRTC output.
- **Opus** — WebRTC native audio codec, v4.0+. Transcoded to AAC for RTMP output. SRS includes built-in AAC↔Opus transcoding.
- **G.711 (PCMU/PCMA)** — WebRTC audio codec, v7.0.124+.

### Audio Codec Support — Only AAC, MP3, and Opus are supported (v7.0.102+). Other audio codecs are rejected.

### Codec Transcoding (Built-in)

- **AAC to Opus** — Automatic when converting RTMP/SRT to WebRTC (`rtmp_to_rtc on`).
- **Opus to AAC** — Automatic when converting WebRTC to RTMP (`rtc_to_rtmp on`).
- **MP3 to Opus** — Automatic when converting RTMP(MP3) to WebRTC. v5.0.118+.

Audio transcoding uses FFmpeg's libavcodec API (linked as a library), not an external FFmpeg process. No built-in video transcoding — SRS transmuxes video without re-encoding. Use external FFmpeg for video transcoding.

### RTSP Output Codec Limitation

SRS RTSP output (`rtmp_to_rtsp on`) only supports **H.264 + AAC**, even though the RTSP protocol itself supports many more codecs. This is an SRS implementation limitation, not a protocol limitation.

## Transport

Which underlying transport (TCP or UDP) each protocol uses in SRS:

- **RTMP** — TCP
- **SRT** — UDP. Built-in reliability and encryption over UDP.
- **WebRTC** — UDP. Also supports TCP, v5.0.60+.
- **HLS** — TCP (HTTP-based).
- **HTTP-FLV** — TCP (HTTP-based).
- **HTTP-TS** — TCP (HTTP-based).
- **MPEG-DASH** — TCP (HTTP-based).
- **RTSP** — TCP. SRS only supports TCP transport (no UDP/RTP interleaved).
- **GB28181** — TCP. PS stream over TCP.
