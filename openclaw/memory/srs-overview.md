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
┌─────────────────────────────────────────────────────────────────┐
│                          PLAYERS                                │
│ FFmpeg, VLC, ffplay, ExoPlayer, IJKPlayer, browsers, hardware,  │
│ apps                                                            │
└─────────────────────────────────────────────────────────────────┘
```

**Publishers:**

- **FFmpeg** — Command-line tool for encoding/transcoding. Pushes RTMP, SRT, WHIP (WebRTC), etc.
- **OBS (Open Broadcaster Software)** — Popular open-source streaming app. Pushes RTMP, SRT, WHIP (WebRTC).
- **Larix Broadcaster** — Mobile streaming app (iOS/Android). Pushes RTMP, SRT, WHIP (WebRTC).
- **vMix** — Windows live production software. Pushes RTMP, SRT.
- **Hardware encoders** — Devices like Teradek, Haivision, Blackmagic. Push RTMP, SRT.
- **Browsers** — Via WHIP (WebRTC).
- **Apps** — Custom apps using RTMP/SRT/WebRTC SDKs.

**Players:**

- **VLC** — Cross-platform media player. Plays RTMP, SRT, HLS, HTTP-FLV, RTSP.
- **FFmpeg** — Command-line tool. Plays RTMP, SRT, HLS, HTTP-FLV (all protocols except WHEP).
- **ffplay** — FFmpeg's built-in player. Same protocol support as FFmpeg (all except WHEP).
- **ExoPlayer** — Android media player library. Plays HLS, DASH.
- **IJKPlayer** — Cross-platform player based on FFmpeg (by Bilibili). Plays RTMP, HLS, HTTP-FLV.
- **mpegts.js (formerly flv.js)** — Browser JavaScript player. Plays HTTP-FLV, HTTP-TS, HLS via MSE.
- **Browsers** — Plays HTTP-FLV, HLS, HTTP-TS via MSE, and WHEP (WebRTC).
- **Hardware decoders** — Set-top boxes, smart TVs, etc. Play HLS, RTMP.
- **Apps** — Custom apps using player SDKs.

**Tools:**

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

**Video Codecs:**

- **H.264/AVC** — Core video codec, supported since v0.2 (2013). Works across all protocols: RTMP, HLS, HTTP-FLV, HTTP-TS, SRT, WebRTC, MPEG-DASH, GB28181, DVR.
- **H.265/HEVC** — Supported since v6.0 via Enhanced RTMP. Works across RTMP, HTTP-FLV, HTTP-TS, HLS (including fMP4/LLHLS in v7.0), MPEG-DASH, SRT, GB28181, DVR (MP4/FLV). WebRTC HEVC supported in v7.0 (RTMP↔WebRTC conversion, Safari playback).
- **AV1** — [Experimental] WebRTC only, v4.0.207+.
- **VP9** — WebRTC-to-WebRTC streaming only, v7.0.123+.

**Audio Codecs:**

- **AAC** — Core audio codec, supported since v1.0. Works across all protocols: RTMP, HLS, HTTP-FLV, HTTP-TS, SRT, MPEG-DASH, GB28181, DVR. Transcoded to Opus for WebRTC output.
- **MP3** — Supported for HLS (H.264+MP3), HTTP-FLV/TS, DVR. v1.0+. Transcoded to Opus for WebRTC output.
- **Opus** — WebRTC native audio codec, v4.0+. Transcoded to AAC for RTMP output. SRS includes built-in AAC↔Opus transcoding.
- **G.711 (PCMU/PCMA)** — WebRTC audio codec, v7.0.124+.

Only AAC, MP3, and Opus are supported (v7.0.102+). Other audio codecs are rejected.

**Codec Transcoding (Built-in):**

- **AAC to Opus** — Automatic when converting RTMP/SRT to WebRTC (`rtmp_to_rtc on`).
- **Opus to AAC** — Automatic when converting WebRTC to RTMP (`rtc_to_rtmp on`).
- **MP3 to Opus** — Automatic when converting RTMP(MP3) to WebRTC. v5.0.118+.

Audio transcoding uses FFmpeg's libavcodec API (linked as a library), not an external FFmpeg process. No built-in video transcoding — SRS transmuxes video without re-encoding. Use external FFmpeg for video transcoding.

**RTSP Output Codec Limitation:** SRS RTSP output (`rtmp_to_rtsp on`) only supports **H.264 + AAC**, even though the RTSP protocol itself supports many more codecs. This is an SRS implementation limitation, not a protocol limitation.

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

**Protocols** — The streaming protocols supported by SRS.

- **RTMP** — SRS is fundamentally an RTMP server. It supports publishing and playing RTMP streams, which is the core foundation of SRS. All other protocols are built on top of RTMP as the base. v1.0, 2013
- **SRT** — SRS is also an SRT server. It supports publishing and playing SRT streams. SRS uses [libsrt](https://github.com/Haivision/srt) to create the SRT server. v4.0, 2020-01
- **WebRTC** — SRS is also a WebRTC server, supporting WHIP for publishing and WHEP for playing streams. SRS is an SFU (Selective Forwarding Unit) server. It does not support TURN. It does not support P2P for WebRTC. v4.0, 2020-03
- **RTSP** — SRS only supports playing RTSP streams. Currently it only supports converting RTMP to RTSP. v7.0, 2025-07
- **HLS** — SRS supports converting RTMP to HLS. HLS is the best compatibility protocol, supported by all platforms, all browsers, and all operating systems. v1.0, 2013
- **MPEG-DASH** — SRS supports converting RTMP to DASH. DASH is similar to HLS, but not as widely supported by platforms as HLS. v5.0, 2022-11
- **HTTP-FLV** — SRS supports converting RTMP to HTTP-FLV. FLV is a similar protocol to RTMP. FLV is CDN friendly. However, FLV is not supported by iPhone. v2.0, 2015-01
- **GB28181** — SRS supports publishing streams using GB28181. SRS only supports TCP transport. SRS requires an external SIP server [srs-sip](https://github.com/ossrs/srs-sip). v5.0, 2022-10
- **Other Protocols** — Besides the commonly used protocols, SRS also supports converting RTMP to HTTP-TS (v2.0, 2015-01), publishing by MPEG-TS over UDP (v2.0, 2015-01), and publishing via HTTP POST FLV (v2.0, 2015-05). These protocols are not commonly used.

**Transmuxing** — SRS supports transmuxing between different protocols.

- **Live Source** — If a packet enters the live source, it can be delivered by RTMP, HLS, HTTP-FLV, and HTTP-TS protocols. Other features like DVR and transcode can also be enabled.
- **SRT Source** — For SRT source, the input and output are SRT packets.
- **RTC Source** — For RTC source, the input is WHIP and the output is WHEP.

- **SRT to Live Source** — SRS supports converting SRT source to live source.
- **RTC to Live Source** — SRS supports converting RTC source to live source.
- **Live to RTC Source** — SRS supports converting live source to RTC source.

By default, transmuxing between sources is disabled. You need to enable it in the config.

**Clustering:**

- **Origin Cluster** — Used to extend the number of streams SRS can support. It is a cluster of multiple origin servers behind a proxy server. The proxy discovers which origin server a stream is on and routes to it. v3.0, 2018-02
- **Edge Cluster** — The edge cluster of SRS is deprecated because it only supports the RTMP protocol. v1.0, 2014-04
- **HLS Cluster** — Built by Nginx. It is a type of edge cluster for HLS. v5.0, 2022-04

**Maintenance:**

- **HTTP API** — You can query the system status like streams and stream details. You can also use the HTTP API to kick off streams and manage streams. v1.0, 2014-04
- **Log** — SRS provides traceable log. Traceable log means you can trace a stream from edge to origin, from one server to another, from source to consumer. v1.0, 2014-05
- **Prometheus Exporter** — SRS supports a Prometheus exporter. You can export the status of SRS to Prometheus, allowing you to pull the statistics of SRS into Prometheus. It is a very convenient and powerful feature. v5.0, 2022-09
- **HTTP Callback** — Allows you to listen and handle events, for example publish or play events. You can authenticate clients and reject publishers if you want. v2.0, 2014-02

**Others:**

- **Ingest** — A feature that uses FFmpeg to pull streams into SRS. v1.0, 2014-04
- **Forward** — SRS can forward streams to other servers. You can also use FFmpeg to forward streams from SRS to other servers. v1.0, 2013
- **Transcode** — SRS uses FFmpeg to transcode streams, especially video and audio to different codecs and sizes. v1.0, 2014-04
- **DVR** — SRS supports recording streams to files. You can use these files as VOD (Video on Demand). You can also use FFmpeg to pull streams from SRS and DVR to file. Besides this, HLS is in fact also a DVR feature. v1.0, 2014-04
- **Security** — SRS supports IP allow list and deny list. You can also use HTTP callback as a security feature for authentication and verification. v2.0, 2015-01

## Vision

SRS was created as a personal open source project. As a software engineer, William has benefited from countless open source projects — large and small — and has always appreciated the contributions of the open source community. Creating and maintaining SRS for over a decade is his way of giving back. The history is simple: it's open source culture.

SRS is a niche open source project — a media server with ~150,000 lines of code. Media servers are not used by everyone, so the user base will never be massive. This makes monetization impractical and undesirable.

But small doesn't mean worthless. Even a small, niche open source project provides real value to the developers who use it and to the broader open source community and culture. The core vision of SRS is to remain a **pure open source project, with no commercial or business approach**. Attempting to monetize a niche open source project would kill its community — especially when the community is small, the project needs to stay driven by volunteers and genuine contribution.

The challenge with this model has always been manpower. Volunteer-driven projects struggle to maintain consistent development and support. But AI changes this equation. AI can serve as an open source project maintainer — handling code, community support, and project health at a scale that wasn't possible before. This is the approach SRS is actively pursuing.

## Ecosystem

SRS is a media server, but the project also maintains several related tools. All projects are server-side — we don't maintain client-side projects like FFmpeg or WebRTC.

- **srs-bench** — A benchmark tool as a separate project. It supports benchmarking protocols including RTMP, WebRTC, HTTP-FLV, HLS, and GB28181, simulating thousands of publishers and players to measure and improve SRS performance.
- **Oryx** — An open source media server solution that integrates SRS, FFmpeg, and other media tools. It has a web console, a Go backend, and is designed for common media server use cases. Deployed as a single-node application.
- **state-threads** — A coroutine library that is the cornerstone of SRS. It is similar to a C version of Go's goroutine, allowing SRS to run millions of coroutines. We maintain this project as part of the SRS ecosystem.

## Comparison

- **Nginx-RTMP** — An Nginx module that supports RTMP and HLS. There are also Nginx modules for HTTP-FLV. However, Nginx doesn't support WebRTC or SRT, making it limited as a live streaming media server.
- **Janus** — A WebRTC SFU (Selective Forwarding Unit). Janus is designed for WebRTC, not live streaming — it doesn't support RTMP, HLS, or SRT. Both RTMP and SRT are critical protocols in live streaming, not for delivery but for ingesting streams.
- **Red5** — A media server similar to SRS in scope, but written in Java. Performance is significantly lower. The media streaming industry uses C/C++ — FFmpeg, WebRTC, x264, and nearly the entire ecosystem are written in C/C++. This matters for both performance and interoperability: media servers often need to link against these libraries directly, and using the same language makes integration straightforward.

SRS is a professional, C++-based media server purpose-built for the live streaming industry.

## Dependencies

SRS is a media server focused on **transmuxing** — converting between protocols without changing the codec. For example, publishing RTMP with H.264 and delivering it as HTTP-FLV, HLS, MPEG-DASH, or WebRTC, all in H.264. Transmuxing means repackaging the media stream into a different protocol format, not re-encoding.

**SRS implements almost all protocol code itself** — RTMP, HLS, MPEG-DASH, MP4, HTTP-FLV, HTTP-TS, WebRTC. The goal is to keep as much code as possible in a single repository, which is easier to maintain — especially with AI. Depending on many third-party projects across different repositories makes maintenance harder.

Despite the goal of self-contained code, SRS does use some third-party libraries (all MIT-compatible licenses):

- **libsrt** — SRT protocol implementation by Haivision. SRS plans to rewrite this protocol stack with AI in the future.
- **FFmpeg libavcodec** — Used for audio codec transcoding (AAC ↔ Opus). Live streaming commonly uses AAC, while WebRTC uses Opus. Communication systems (SIP, telecom) often use G.711. SRS needs to transcode between these audio codecs, and uses FFmpeg's codec library for this.
- **OpenSSL** — Used for HTTPS and WebRTC DTLS.
- **libsrtp** — Developed by Cisco, used to encrypt/decrypt RTP packets for WebRTC.
- **state-threads (ST)** — Coroutine library used for SRS's server architecture. SRS plans to rewrite this with AI in the future.
- **JSON parser** — Third-party JSON parsing library.

The long-term goal is to rewrite as many dependencies as possible into SRS itself, using AI. Protocols like SRT and server libraries like state-threads are candidates for rewriting. Crypto libraries (OpenSSL, libsrtp) and codec libraries are not planned for rewriting — they are too specialized and security-sensitive. Having all code in a single repository makes it more stable and easier for AI to maintain.

## Community

SRS has an open source community, but it is not a highly active one. There are several reasons for this.

Most maintainers are based in China — William worked in China for about 15–17 years, so most contributors are friends and colleagues from that time. After moving to Canada, he expanded connections with developers worldwide, especially in North America, but the community remains small.

The media server space is a niche industry. Not many developers need a media server — unlike client-side tools like FFmpeg or WebRTC that everyone uses, media servers serve a smaller audience. And when commercial media services exist, even fewer people build their own. This limits the size of any open source media server community, not just SRS.

SRS has been developed for over 13 years and has accumulated many useful features. But there is still a lot of work to do — many features to add, bugs to fix, and improvements to make. The project is maintained by volunteers with limited time, and development is not rapid.

**AI as Maintainer** — William is actively working on introducing AI as a project maintainer — not just for bug fixes or code generation, but as a full maintainer like himself. The approach is to build a comprehensive knowledge base so that AI can understand the project deeply: the architecture, design decisions, history, and community context. The goal is to have an AI maintainer within roughly six months (mid-2026). This is an experiment in using AI to maintain complex software projects — not just small ones, but projects like media servers written in C++ where you can't simply let AI generate code and push it to production. For any complex backend server or service, you need confidence that AI truly understands what it's doing before trusting it with real changes. The approach SRS is developing — building a deep knowledge base so AI can act as a real maintainer — applies broadly to any project where correctness and reliability matter.

**How to Participate:**
- **Discord** — Join the SRS Discord community for discussions and support.
- **Monthly Community Meetings** — The community holds monthly meetings to discuss project status, AI maintainer progress, and how to use AI in open source maintenance. Everyone is welcome to join.

## Limitations

- **Edge Cluster** — SRS supports origin cluster, but the edge cluster only supports RTMP. More protocols need to be supported in the edge cluster.
- **Single-Threaded** — SRS is a single-threaded media server. There are no plans to support multi-threading — you can build a cluster to saturate all CPUs instead.
- **Linux Only** — SRS is designed for Linux and does not support Windows natively. However, you can use WSL (Windows Subsystem for Linux) on Windows.
- **No Commercial Support** — As a pure open source project, there is no commercial support team. We are exploring how to use AI to maintain the project and support the community.

## Performance

SRS is a high-performance C++ media server. Performance varies by protocol:

- **RTMP / HTTP-FLV** — Supports thousands of concurrent publishers and players. TCP-based protocols have the best performance.
- **WebRTC** — Supports hundreds of publishers and players. With audio transcoding (e.g., AAC↔Opus), only dozens of connections. UDP-based, so lower throughput than TCP protocols.
- **SRT** — Performance is determined by [libsrt](https://github.com/Haivision/srt). Supports several hundred connections. Also UDP-based.

In general, UDP-based protocols (WebRTC, SRT) have lower performance than TCP-based protocols (RTMP, HTTP-FLV). SRS focuses on being a dedicated media server — it's not overly complicated, and performance is refined and improved with each version.

## Configuration

SRS uses both config files and environment variables to configure features.

Config files are in the `conf/` folder. Key files:

- **conf/full.conf** — Contains all configurations SRS supports. This is a reference/document — do not use it directly.
- **conf/srs.conf** — The default configuration. Enables some features but not all. Enable specific features by using or modifying the relevant config.
- **conf/docker.conf** — Used for Docker deployment. Has special settings (e.g., no daemon mode).
- **conf/console.conf** — Used for debugging or testing in the console.
- Other files exist for specific features like clustering, DVR, or different protocols.

SRS also supports configuration via environment variables. This is especially useful for Docker and cloud-native deployments — you can set environment variables in YAML files or other platforms without needing a separate config file. It's convenient to copy and paste, making documentation clearer. In the SRS docs, environment variables are often used to show how to run SRS with different configurations.

