---
title: Client SDK
sidebar_label: Client SDK
hide_title: false
hide_table_of_contents: false
---

# Client SDK

The workflow of live streaming:

```
+---------+      +-----------------+       +---------+
| Encoder +-->---+ SRS/CDN Network +--->---+ Player  |
+---------+      +-----------------+       +---------+
```

## EXOPlayer

The [EXOPlayer](https://github.com/google/ExoPlayer) is a Android player which support HTTP-FLV and HLS.

## IJKPlayer

[ijkplayer](https://github.com/Bilibili/ijkplayer) is a player from [bilibili](http://www.bilibili.com/), for both Android and iOS.

## FFmpeg

[FFmpeg](https://ffmpeg.org) is a complete, cross-platform solution to record, convert and stream audio and video.

## LIBRTMP

The [LIBRTMP](https://github.com/ossrs/librtmp) or [SRS-LIBRTMP](https://github.com/ossrs/srs-librtmp) only provides transport over RTMP.

## WebRTC

[WebRTC](https://webrtc.org/) is Real-time communication for the web.

## PC

Although the number of PC users are smaller, there are still some use scenarios for [OBS](https://obsproject.com).

> Remark: For publishing by OBS, the **Stream Key** should be filled by stream name.

![OBS](/img/doc-integration-client-sdk-001.png)

![OBS](/img/doc-integration-client-sdk-002.png)

Winlin 2017.4

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/client-sdk)


