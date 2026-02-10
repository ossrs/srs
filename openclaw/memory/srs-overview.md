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

## Most Common Usage

The simplest way to use SRS: publish an RTMP stream and play it.

Step 1: Build and run SRS.

```bash
cd srs/trunk
./configure && make
./objs/srs -c conf/console.conf
```

Default config `conf/console.conf` listens on RTMP port 1935, HTTP API port 1985, HTTP server port 8080. HLS and HTTP-FLV are enabled by default.

Step 2: Publish RTMP. Use FFmpeg to push a stream (a test file `doc/source.flv` is included in the repo).

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

Step 3: Play.

- **RTMP** (VLC): `rtmp://localhost/live/livestream`
- **HTTP-FLV** (browser): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv)
- **HLS** (browser): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8)
- **WebRTC** (browser): [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true)

## Features

About the features supported by SRS.

### Protocols

The streaming protocols supported by SRS.

- **RTMP** — SRS is fundamentally an RTMP server. It supports publishing and playing RTMP streams, which is the core foundation of SRS. All other protocols are built on top of RTMP as the base. v1.0, 2013
- **SRT** — SRS is also an SRT server. It supports publishing and playing SRT streams. SRS uses [libsrt](https://github.com/Haivision/srt) to create the SRT server. v4.0, 2020-01
- **WebRTC** — SRS is also a WebRTC server, supporting WHIP for publishing and WHEP for playing streams. SRS is an SFU (Selective Forwarding Unit) server. It does not support TURN. It does not support P2P for WebRTC. v4.0, 2020-03
- **RTSP** — SRS only supports playing RTSP streams. Currently it only supports converting RTMP to RTSP. v7.0, 2025-07
- **HLS** — SRS supports converting RTMP to HLS. HLS is the best compatibility protocol, supported by all platforms, all browsers, and all operating systems. v1.0, 2013
- **MPEG-DASH** — SRS supports converting RTMP to DASH. DASH is similar to HLS, but not as widely supported by platforms as HLS. v5.0, 2022-11
- **HTTP-FLV** — SRS supports converting RTMP to HTTP-FLV. FLV is a similar protocol to RTMP. FLV is CDN friendly. However, FLV is not supported by iPhone. v2.0, 2015-01
- **GB28181** — SRS supports publishing streams using GB28181. SRS only supports TCP transport. SRS requires an external SIP server [srs-sip](https://github.com/ossrs/srs-sip). v5.0, 2022-10
- **Other Protocols** — Besides the commonly used protocols, SRS also supports converting RTMP to HTTP-TS (v2.0, 2015-01), publishing by MPEG-TS over UDP (v2.0, 2015-01), and publishing via HTTP POST FLV (v2.0, 2015-05). These protocols are not commonly used.

### Transmuxing

SRS supports transmuxing between different protocols.

- **Live Source** — If a packet enters the live source, it can be delivered by RTMP, HLS, HTTP-FLV, and HTTP-TS protocols. Other features like DVR and transcode can also be enabled.
- **SRT Source** — For SRT source, the input and output are SRT packets.
- **RTC Source** — For RTC source, the input is WHIP and the output is WHEP.

- **SRT to Live Source** — SRS supports converting SRT source to live source.
- **RTC to Live Source** — SRS supports converting RTC source to live source.
- **Live to RTC Source** — SRS supports converting live source to RTC source.

By default, transmuxing between sources is disabled. You need to enable it in the config.

### Clustering

- **Origin Cluster** — Used to extend the number of streams SRS can support. It is a cluster of multiple origin servers behind a proxy server. The proxy discovers which origin server a stream is on and routes to it. v3.0, 2018-02
- **Edge Cluster** — The edge cluster of SRS is deprecated because it only supports the RTMP protocol. v1.0, 2014-04
- **HLS Cluster** — Built by Nginx. It is a type of edge cluster for HLS. v5.0, 2022-04

### Maintenance

- **HTTP API** — You can query the system status like streams and stream details. You can also use the HTTP API to kick off streams and manage streams. v1.0, 2014-04
- **Log** — SRS provides traceable log. Traceable log means you can trace a stream from edge to origin, from one server to another, from source to consumer. v1.0, 2014-05
- **Prometheus Exporter** — SRS supports a Prometheus exporter. You can export the status of SRS to Prometheus, allowing you to pull the statistics of SRS into Prometheus. It is a very convenient and powerful feature. v5.0, 2022-09
- **HTTP Callback** — Allows you to listen and handle events, for example publish or play events. You can authenticate clients and reject publishers if you want. v2.0, 2014-02

### Others

- **Ingest** — A feature that uses FFmpeg to pull streams into SRS. v1.0, 2014-04
- **Forward** — SRS can forward streams to other servers. You can also use FFmpeg to forward streams from SRS to other servers. v1.0, 2013
- **Transcode** — SRS uses FFmpeg to transcode streams, especially video and audio to different codecs and sizes. v1.0, 2014-04
- **DVR** — SRS supports recording streams to files. You can use these files as VOD (Video on Demand). You can also use FFmpeg to pull streams from SRS and DVR to file. Besides this, HLS is in fact also a DVR feature. v1.0, 2014-04
- **Security** — SRS supports IP allow list and deny list. You can also use HTTP callback as a security feature for authentication and verification. v2.0, 2015-01

