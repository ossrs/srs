---
slug: unlock-the-power-of-hevc-via-rtmp
title: SRS - Unlock the Power of HEVC via RTMP - Boost Your Live Streaming with OBS and FFmpeg
authors: []
tags: [hevc, rtmp, obs, ffmpeg, live]
custom_edit_url: null
---

# Unlock the Power of HEVC via RTMP - Boost Your Live Streaming with OBS and FFmpeg

## Introduction

HEVC, or H.265, can reduce bandwidth usage by about 50% compared to the widely used H.264 codec, which has the 
best compatibility. Over the past 10 years, HEVC has grown slowly because a new codec needs an ecosystem to 
support it, including decoders and device hardware. Now, both RTMP and FLV support HEVC in OBS and FFmpeg, 
which are the standard tools in the live streaming industry.

<!--truncate-->

In live streaming, all the necessary tools are ready for HEVC. For encoding, FFmpeg and OBS are the most commonly 
used streaming tools. OBS 29+ supports RTMP with HEVC, while FFmpeg 6 supports RTMP with HEVC. For HTML5 players, 
mpegts.js 1.7.3 already supports HEVC via HTTP-FLV or HTTP-TS. VLC and ffplay also support HEVC via HTTP-TS, SRT, 
and HLS. For media servers, SRS 6 supports HEVC via RTMP, FLV, TS, HLS, SRT, WebRTC, and more.

In this tutorial, we introduce FFmpeg 6, which added HEVC support just a few months ago. This is a significant 
development, as many tools that use FFmpeg can now utilize HEVC via RTMP without any patches.

> Note: If you are interested in learning about OBS support for HEVC via RTMP, please read 
> [How to Push HEVC via RTMP by OBS](./2023-04-08-Push-HEVC-via-RTMP-by-OBS.md).

> Note: Please note that SRS 6.0 had already HEVC(H.265) support for RTMP, HTTP-FLV, SRT, HTTP-TS, HLS, MPEG-DASH 
> and WebRTC(Safari), please refer to [H.265 Live Streaming Saving 50% Cost](./2023-03-07-Lets-Do-H265-Live-Streaming.md).

## Step 1: Run SRS Media Server

YouTube supports HEVC through RTMP, allowing you to directly publish HEVC via RTMP streams to YouTube using 
OBS or FFmpeg. If you need to create your own live streaming platform, you can use the SRS media server, which 
also supports HEVC via RTMP. 

To run SRS, you can use Docker with the following command:

```
docker run --rm -it -p 1935:1935 -p 8080:8080 ossrs/srs:6 \
  ./objs/srs -c conf/docker.conf
```

To check if it started successfully, open your browser and visit 
[http://localhost:8080](http://localhost:8080).

## Step 2: Publish by FFmpeg 6

If you have compiled FFmpeg 6 from source code using the instructions from this [post](../docs/v6/doc/hevc#ffmpeg-tools), 
you can now utilize FFmpeg to publish RTMP HEVC streams:

```bash
ffmpeg -stream_loop -1 -re -i doc/source.flv -acodec copy \
  -vcodec libx265 -f flv rtmp://localhost/live/livestream
```

Keep in mind that FFmpeg 6.0 does not support HEVC over RTMP until the following commit
[637c761b](https://github.com/FFmpeg/FFmpeg/commit/637c761be1bf9c3e1f0f347c5c3a390d7c32b282):

```
commit 637c761be1bf9c3e1f0f347c5c3a390d7c32b282
Author: Steven Liu <liuqi05@kuaishou.com>
Date:   Mon Aug 28 09:59:24 2023 +0800

    avformat/rtmpproto: support enhanced rtmp
    
    add option named rtmp_enhanced_codec,
    it would support hvc1,av01,vp09 now,
    the fourcc is using Array of strings.
    
    Signed-off-by: Steven Liu <lq@chinaffmpeg.org>
```

SRS offers Docker support for FFmpeg versions 4 and 5, which enable HEVC streaming through RTMP. You can 
conveniently use Docker to directly publish your live stream.

```bash
# For macOS
docker run --rm -it ossrs/srs:encoder \
  ffmpeg -stream_loop -1 -re -i doc/source.flv -acodec copy \
    -vcodec libx265 -f flv rtmp://host.docker.internal/live/livestream

# For Linux
docker run --net=host --rm -it ossrs/srs:encoder \
  ffmpeg -stream_loop -1 -re -i doc/source.flv -acodec copy \
    -vcodec libx265 -f flv rtmp://127.0.0.1/live/livestream
```

> Note: If want to use OBS, please download OBS 29+ from [here](https://github.com/obsproject/obs-studio/releases),
> select the HEVC codec, please see [How to Push HEVC via RTMP by OBS](./2023-04-08-Push-HEVC-via-RTMP-by-OBS.md)
> for detail.

## Step 3: Play by HTML5 Player

To play HEVC streams in your web browser, you can utilize mpegts.js version 1.7.3+ for playing HTTP-FLV or 
HTTP-TS. 

The SRS player has mpegts.js integrated, so all you need to do is open the URL to play the HEVC stream:

* HTTP-FLV(by H5):  [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true)

When it comes to HLS streaming, hls.js doesn't support HEVC via TS. As a result, you should use VLC or ffplay 
to play the HLS stream instead:

* HLS(by VLC or fflay): `http://localhost:8080/live/livestream.m3u8`

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

In summary, using HEVC for live streaming has become more accessible thanks to essential tools like OBS, FFmpeg, 
and SRS media server, making it simpler for users to benefit from the bandwidth savings provided by this codec. 
By following the steps in this guide, you can create your own live streaming platform with HEVC and RTMP, and 
play the streams using HTML5 players like mpegts.js, or VLC and ffplay for HLS streaming.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/23-10-26-Unlock-the-Power-of-HEVC-via-RTMP)
