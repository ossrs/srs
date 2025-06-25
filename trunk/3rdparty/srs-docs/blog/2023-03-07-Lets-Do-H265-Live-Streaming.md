---
slug: lets-do-h265-live-streaming
title: SRS - H.265 Live Streaming Saving 50% Cost
authors: []
tags: [architecture, hevc, h265, codec, live]
custom_edit_url: null
---

# Let's Do H265 Live Streaming

> Written by [Winlin](https://github.com/winlinvip), [runner365](https://github.com/runner365), [yinjiaoyuan](https://github.com/yinjiaoyuan), [PieerePi](https://github.com/PieerePi), [qichaoshen82](https://github.com/qichaoshen82), [ZSC714725](https://github.com/ZSC714725), [bluestn](https://github.com/bluestn), [mapengfei53](https://github.com/mapengfei53), [chundonglinlin](https://github.com/chundonglinlin), [duiniuluantanqin](https://github.com/duiniuluantanqin), [panda1986](https://github.com/panda1986)

SRS 6.0 supports HEVC(H.265), for RTMP, HTTP-FLV, HTTP-TS, HLS, MPEG-DASH, WebRTC(Safari), DVR FLV, DVR MP4 and WordPress SrsPlayer, etc.

Generally, H.265 is 50% off than H.264, so you only need to pay 50% bills if H.265.

<!--truncate-->

Now, you're able to use HEVC(H.265) in live streaming.

## Why Important?

H.265 uses less bandwidth than H.264, which means lower bandwidth cost, or higher quality in same bandwidth.

Furthermore, if want 8K live streaming, you must use H.265, because H.264 doesn't support 8K resolution.

Please note that there are still some traps for H.265 live streaming.

## Status of H.265

The status of H.265, for different use scenarios.

Part 1, publisher or encoder for H.265.

* [x] Publish SRT stream by FFmpeg.
* [x] Publish SRT stream by OBS.
* [x] Publish RTMP stream by FFmpeg, patch is [here](https://github.com/ossrs/srs/issues/465#ffmpeg-tools)
* [x] Push WebRTC by Safari, should be enabled by user.
* [ ] Not supported: Chrome/Firefox push WebRTC stream.
* [ ] Not supported: OBS publish RTMP stream.

Part 2, play stream by FFmpeg/ffplay for H.265.

* [x] Play HTTP-TS by FFmpeg.
* [x] Play HLS by FFmpeg.
* [x] Play MPEG-DASH by FFmpeg.
* [x] Play SRT by FFmpeg.
* [x] Play HTTP-TS by ffplay.
* [x] Play HLS by ffplay.
* [x] Play MPEG-DASH by ffplay.
* [x] Play SRT by ffplay.
* [x] Play RTMP by FFmpeg, with [patch](https://github.com/ossrs/srs/issues/465#ffmpeg-tools).
* [x] Play HTTP-FLV by FFmpeg, with [patch](https://github.com/ossrs/srs/issues/465#ffmpeg-tools).
* [x] Play RTMP by fflay, with [patch](https://github.com/ossrs/srs/issues/465#ffmpeg-tools).
* [x] Play HTTP-FLV by ffplay, with [patch](https://github.com/ossrs/srs/issues/465#ffmpeg-tools).

Part 3, play stream by H5 MSE for H.265.

* [x] Play HTTP-TS by Chrome, using mpegts.js.
* [x] Play HTTP-FLV by Chrome, using mpegts.js.
* [x] Play WebRTC stream by Safari, should be enabled by user.
* [ ] Not supported: Play HLS by [hls.js](https://github.com/video-dev/hls.js).
* [ ] Not supported: Play MPEG-DASH by [dash.js](https://github.com/Dash-Industry-Forum/dash.js).
* [ ] Not supported: Play WebRTC by Chrome/Firefox.

Part 4, play stream by VLC for H.265.

* [x] Play HTTP-TS by VLC.
* [x] Play SRT by VLC.
* [x] Play HLS by VLC.
* [x] Play MPEG-DASH by VLC.
* [ ] Not supported: Play RTMP by VLC.
* [ ] Not supported: Play HTTP-FLV by VLC.

Part 5, other features for H.265.

* [x] DVR to FLV or MP4 file.
* [x] Parse HEVC metadata for HTTP-API.
* [x] Black box test support HEVC.
* [x] Patch FFmpeg in SRS dev-docker.
* [x] Support HEVC for [WordPress plugin SrsPlayer](https://github.com/ossrs/WordPress-Plugin-SrsPlayer).
* [ ] Not supported: Update [Oryx](https://github.com/ossrs/oryx) for HEVC.
* [ ] Not supported: Edge server supports publish HEVC stream to origin.
* [ ] Not supported: Edge server supprots play HEVC stream from origin.
* [ ] Not supported: HTTP Callback takes HEVC metadata.
* [ ] Not supported: Prometheus Exporter supports HEVC metadata.
* [ ] Not supported: Improve coverage for HEVC.
* [ ] Not supported: Supports benchmark for HEVC by [srs-bench](https://github.com/ossrs/srs-bench).

If native iOS or Android application, please use FFmpeg.

Appreciate the patch for [Chrome 105](https://zhuanlan.zhihu.com/p/541082191) to support HEVC for MSE, it makes the HEVC
live streaming possible.

MSE HEVC requires GPU hardware decoding, please open `chrome://gpu` then search `hevc`, and it should work if matched.

## Usage: Live

This is an example fo H.265 live streaming.

First, run SRS in docker:

```bash
docker run --rm -it -p 1935:1935 -p 8080:8080 ossrs/srs:6 \
  ./objs/srs -c conf/docker.conf
```

Use FFmepg to publish HEVC over RTMP, see [FFmpeg Tools](#ffmpeg-tools):

```bash
# For macOS
docker run --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -acodec copy -vcodec libx265 -f flv rtmp://host.docker.internal/live/livestream

# For linux
docker run --net=host --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -acodec copy -vcodec libx265 -f flv rtmp://127.0.0.1/live/livestream
```

> Note: Please change the ip `host.docker.internal` to your SRS's IP.

Done, please open links bellow in browser:

* [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?stream=livestream.flv)
* `http://localhost:8080/live/livestream.m3u8`

> Note: Please use VLC or ffplay to play HLS, because hls.js doesn't support it.

Please follow wiki of SRS to use other protocols.

## Usage: WebRTC

Right now, at 2023.03, only Safari support HEVC over WebRTC, neither Chrome nor Firefox supports it.

And, you should enable the HEVC for Safari by clicking `Develop > Experimental Features > WebRTC H265 codec`.

For detail usage, please follow [#465](https://github.com/ossrs/srs/issues/465#safari-webrtc).

Beside HEVC, WebRTC has better support for AV1, Safari/Chrome/Firefox supports AV1, please read [#1070](https://github.com/ossrs/srs/issues/1070) for detail.
Note that MSE doesn't support AV1.

> Note: Media Source Extensions (MSE) is H5 API for media streaming, on which mpegts.js, hls.js and dash.js bases. Please see [MDN: MSE](https://developer.mozilla.org/en-US/docs/Web/API/Media_Source_Extensions_API).

## FFmpeg Patch

FFmpeg 6 has supported HEVC over RTMP, if you want to build from code, please see
[FFmpeg Tools](../docs/v6/doc/hevc#ffmpeg-tools#ffmpeg-tools).

SRS dev-docker already patched FFmpeg, ffplay and ffprobe, so user can use it:

```bash
# For macOS
docker run --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -acodec copy -vcodec libx265 -f flv rtmp://host.docker.internal/live/livestream

# For linux
docker run --net=host --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -acodec copy -vcodec libx265 -f flv rtmp://127.0.0.1/live/livestream
```

Please see [FFmpeg Tools](../docs/v6/doc/hevc#ffmpeg-tools#ffmpeg-tools) for detail.

## Known Issues

The known issues for HEVC live streaming:

1. Safari HEVC WebRTC doesn't support converting to other protocols, such as RTMP.
2. Chrome/Firefox WebRTC doesn't support HEVC, no plan also.
3. All browsers support MSE except iOS, and note that HEVC MSE requires GPU hardware decoding.
4. For H5 player, only mpegts.js supports HEVC, neither hls.js nor dash.js support it.

On some use scenario, HEVC is available now, please evaluate it by yourself.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Thanks

There are some developers that contributed to SRS HEVC feature:

* [runner365](https://github.com/runner365) The first patch for HEVC for RTMP, HLS and SRT.
* [yinjiaoyuan](https://github.com/yinjiaoyuan) Bug fixed.
* [PieerePi](https://github.com/PieerePi) Bug fixed.
* [qichaoshen82](https://github.com/qichaoshen82) Bug fixed.
* [ZSC714725](https://github.com/ZSC714725) Bug fixed.
* [bluestn](https://github.com/bluestn) Support HEVC for MP4 and GB28181.
* [mapengfei53](https://github.com/mapengfei53) Support HEVC for MP4.
* [chundonglinlin](https://github.com/chundonglinlin) Support HEVC for SRT.
* [duiniuluantanqin](https://github.com/duiniuluantanqin) Support HEVC for GB28181.
* [panda1986](https://github.com/panda1986) Supprot HEVC for WordPress SrsPlayer.

Really appreciated for [mpegts.js](https://github.com/xqq/mpegts.js), which supports HEVC for HTTP-FLV and HTTP-TS.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/23-03-07-Lets-Do-H265-Live-Streaming)
