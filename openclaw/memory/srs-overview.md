# SRS Overview

## What is SRS

SRS is a **simple, high-efficiency, real-time media server**. It receives streams from publishers and delivers them to players.

---

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
│   VLC, ffplay, ExoPlayer, IJKPlayer, browsers, hardware, apps   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Protocols (Each Supports Input AND Output)

| Protocol | Publishers Use | Players Use | Typical Scenario |
|----------|----------------|-------------|------------------|
| **RTMP** | OBS, FFmpeg, Larix | VLC, ffplay | Traditional live streaming |
| **SRT** | OBS, vMix, hardware | ffplay, VLC, hardware | Long-distance, professional broadcast |
| **WebRTC** | Browsers, apps | Browsers, apps | Real-time communication, conferences |
| **HLS/HTTP-FLV** | — | ExoPlayer, mpegts.js, browsers | Wide compatibility playback |

---

## Tools

| Tool | Homepage |
|------|----------|
| FFmpeg | https://ffmpeg.org |
| OBS | https://obsproject.com |
| Larix Broadcaster | https://softvelum.com/larix/ |
| vMix | https://www.vmix.com |
| VLC | https://www.videolan.org/vlc/ |
| ffplay | https://ffmpeg.org/ffplay.html |
| ExoPlayer | https://developer.android.com/media/exoplayer |
| IJKPlayer | https://github.com/bilibili/ijkplayer |
| mpegts.js (formerly flv.js) | https://github.com/xqq/mpegts.js |
