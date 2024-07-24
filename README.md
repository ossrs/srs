# SRS(Simple Realtime Server)

![](http://ossrs.net/gif/v1/sls.gif?site=github.com&path=/srs/develop)
[![](https://github.com/ossrs/srs/actions/workflows/codeql-analysis.yml/badge.svg?branch=develop)](https://github.com/ossrs/srs/actions?query=workflow%3ACodeQL+branch%3Adevelop)
[![](https://github.com/ossrs/srs/actions/workflows/release.yml/badge.svg)](https://github.com/ossrs/srs/actions/workflows/release.yml?query=workflow%3ARelease)
[![](https://github.com/ossrs/srs/actions/workflows/test.yml/badge.svg?branch=develop)](https://github.com/ossrs/srs/actions?query=workflow%3ATest+branch%3Adevelop)
[![](https://codecov.io/gh/ossrs/srs/branch/develop/graph/badge.svg?token=Zx2LhdtA39)](https://app.codecov.io/gh/ossrs/srs/tree/develop)
[![](https://ossrs.net/wiki/images/wechat-badge4.svg)](https://ossrs.net/lts/zh-cn/contact#discussion)
[![](https://img.shields.io/twitter/follow/srs_server?style=social)](https://twitter.com/srs_server)
[![](https://img.shields.io/badge/SRS-YouTube-red)](https://www.youtube.com/@srs_server)
[![](https://badgen.net/discord/members/yZ4BnPmHAd)](https://discord.gg/yZ4BnPmHAd)
[![](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fossrs%2Fsrs.svg?type=small)](https://app.fossa.com/projects/git%2Bgithub.com%2Fossrs%2Fsrs?ref=badge_small)
[![](https://badgen.net/badge/srs/stackoverflow/orange?icon=terminal)](https://stackoverflow.com/questions/tagged/simple-realtime-server)
[![](https://opencollective.com/srs-server/tiers/badge.svg)](https://opencollective.com/srs-server)
[![](https://img.shields.io/docker/pulls/ossrs/srs)](https://hub.docker.com/r/ossrs/srs/tags)

SRS/6.0 ([Hang](https://ossrs.io/lts/en-us/product#release-60)) is a simple, high-efficiency, and real-time video server, 
supporting RTMP/WebRTC/HLS/HTTP-FLV/SRT/MPEG-DASH/GB28181, Linux/Windows/macOS, X86_64/ARMv7/AARCH64/M1/RISCV/LOONGARCH/MIPS, 
and essential [features](trunk/doc/Features.md#features).

[![SRS Overview](https://ossrs.net/wiki/images/SRS-SingleNode-4.0-sd.png?v=114)](https://ossrs.net/wiki/images/SRS-SingleNode-4.0-hd.png)

> Note: For more details on the single-node architecture for SRS, please visit the following [link](https://www.figma.com/file/333POxVznQ8Wz1Rxlppn36/SRS-4.0-Server-Arch).

SRS is licenced under [MIT](https://github.com/ossrs/srs/blob/develop/LICENSE), and some third-party libraries are 
distributed under their [licenses](https://ossrs.io/lts/en-us/license).

<a name="product"></a> <a name="usage-docker"></a>

## Usage

Please check the Getting Started guide in [English](https://ossrs.io/lts/en-us/docs/v5/doc/getting-started) 
or [Chinese](https://ossrs.net/lts/zh-cn/docs/v5/doc/getting-started). We highly recommend using SRS with docker:

```bash
docker run --rm -it -p 1935:1935 -p 1985:1985 -p 8080:8080 \
    -p 8000:8000/udp -p 10080:10080/udp ossrs/srs:5
```

> Tips: If you're in China, use this image `registry.cn-hangzhou.aliyuncs.com/ossrs/srs:5` for faster speed.

Open [http://localhost:8080/](http://localhost:8080/) to verify, and then stream using the following
[FFmpeg](https://ffmpeg.org/download.html) command:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

Alternatively, stream by [OBS](https://obsproject.com/download) using the following configuration:

* Service: `Custom`
* Server: `rtmp://localhost/live`
* Stream Key: `livestream`

Play the following streams using media players:

* To play an RTMP stream with URL `rtmp://localhost/live/livestream` on [VLC player](https://www.videolan.org/), open the player, go to Media > Open Network Stream, enter the URL and click Play.
* You can play HTTP-FLV stream URL [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv) on a webpage using the srs-player, an HTML5-based player.
* Use srs-player for playing HLS stream with URL [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8).

If you'd like to use WebRTC, convert RTMP to WebRTC, or convert WebRTC to RTMP, please check out 
the wiki documentation in either [English](https://ossrs.io/lts/en-us/docs/v5/doc/getting-started#webrtc) or 
[Chinese](https://ossrs.net/lts/zh-cn/docs/v5/doc/getting-started#webrtc).

To learn more about RTMP, HLS, HTTP-FLV, SRT, MPEG-DASH, WebRTC protocols, clustering, 
HTTP API, DVR, and transcoding, please check the documents in [English](https://ossrs.io) 
or [Chinese](https://ossrs.net).

## Sponsor

Would you like additional assistance from us? By becoming a sponsor or backer of SRS, we can provide you 
with the support you need:

* Backer: $5 per month, online text chat support through Discord.
* Sponsor: $100 per month, online text chat plus online meeting support.

Please visit [OpenCollective](https://opencollective.com/srs-server) to become a backer or sponsor, and send 
us a direct message on [Discord](https://discord.gg/yZ4BnPmHAd). We are currently providing support to the 
developers listed below:

[![](https://opencollective.com/srs-server/backers.svg?width=800&button=false)](https://opencollective.com/srs-server)

At SRS, our goal is to create a free, open-source community that helps developers all over the world 
build high-quality streaming and RTC platforms for their businesses.

<a name="authors"></a>

## Contributing

The [authors](trunk/AUTHORS.md#authors), [TOC(Technical Oversight Committee)](trunk/AUTHORS.md#toc), 
and [contributors](trunk/AUTHORS.md#contributors) are listed [here](trunk/AUTHORS.md). The TOC members 
who made significant contributions and maintained parts of SRS are listed below:

* [Winlin](https://github.com/winlinvip): Founder of the project, focusing on ST and Issues/PR. Responsible for architecture and maintenance.
* [ZhaoWenjie](https://github.com/wenjiegit): One of the earliest contributors, focusing on HDS and Windows. Has expertise in client technology.
* [ShiWei](https://github.com/runner365): Specializes in SRT and H.265, maintaining SRT and FLV patches for FFmpeg. An expert in codecs and FFmpeg.
* [XiaoZhihong](https://github.com/xiaozhihong): Concentrates on WebRTC/QUIC and SRT, with expertise in network QoS. Contributed to ARM on ST and was the original contributor for WebRTC.
* [WuPengqiang](https://github.com/Bepartofyou): Focused on H.265, initially contributed to the FFmpeg module in SRS for transcoding AAC with OPUS for WebRTC.
* [XiaLixin](https://github.com/xialixin): Specializes in GB28181, with expertise in live streaming and WebRTC.
* [LiPeng](https://github.com/lipeng19811218): Concentrates on WebRTC and contributes to memory management and smart pointers.
* [ChenGuanghua](https://github.com/chen-guanghua): Focused on WebRTC/QoS and introduced the Asan toolchain to SRS.
* [ChenHaibo](https://github.com/duiniuluantanqin): Specializes in GB28181 and HTTP API, contributing to patches for FFmpeg with WHIP.
* [ZhangJunqin](https://github.com/chundonglinlin): Focused on H.265, Prometheus Exporter, and API module.

A huge `THANK YOU` goes out to:

* All the [contributors](trunk/AUTHORS.md#contributors) of SRS.
* All the friends of SRS who gave [big support](https://ossrs.net/lts/zh-cn/product).
* [Genes](http://sourceforge.net/users/genes), [Mabbott](http://sourceforge.net/users/mabbott), and [Michael Talyanksy](https://github.com/michaeltalyansky) for making and sharing [State Threads](https://github.com/ossrs/state-threads/tree/srs).

We're really thankful to everyone in the community for helping us find bugs and improve the project. 
To stay in touch and keep helping our community, please check out this [guide](https://github.com/ossrs/srs/contribute).

## LICENSE

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fossrs%2Fsrs.svg?type=small)](https://app.fossa.com/projects/git%2Bgithub.com%2Fossrs%2Fsrs?ref=badge_small)

SRS is licenced under [MIT](https://github.com/ossrs/srs/blob/develop/LICENSE), and some third-party libraries are 
distributed under their [licenses](https://ossrs.io/lts/en-us/license).

[![](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fossrs%2Fsrs.svg?type=large)](https://app.fossa.com/projects/git%2Bgithub.com%2Fossrs%2Fsrs?ref=badge_large)

## Releases

* 2024-07-04, [Release v6.0-d6](https://github.com/ossrs/srs/releases/tag/v6.0-d6), v6.0-d6, 6.0 dev6, v6.0.134, 168904 lines.
* 2024-06-15, [Release v6.0-d5](https://github.com/ossrs/srs/releases/tag/v6.0-d5), v6.0-d5, 6.0 dev5, v6.0.129, 168454 lines.
* 2024-02-15, [Release v6.0-d4](https://github.com/ossrs/srs/releases/tag/v6.0-d4), v6.0-d4, 6.0 dev4, v6.0.113, 167695 lines.
* 2023-11-19, [Release v6.0-d3](https://github.com/ossrs/srs/releases/tag/v6.0-d3), v6.0-d3, 6.0 dev3, v6.0.101, 167560 lines.
* 2023-09-28, [Release v6.0-d2](https://github.com/ossrs/srs/releases/tag/v6.0-d2), v6.0-d2, 6.0 dev2, v6.0.85, 167509 lines.
* 2023-08-31, [Release v6.0-d1](https://github.com/ossrs/srs/releases/tag/v6.0-d1), v6.0-d1, 6.0 dev1, v6.0.72, 167135 lines.
* 2023-07-09, [Release v6.0-d0](https://github.com/ossrs/srs/releases/tag/v6.0-d0), v6.0-d0, 6.0 dev0, v6.0.59, 166739 lines.
* 2024-06-15, [Release v5.0-r3](https://github.com/ossrs/srs/releases/tag/v5.0-r3), v5.0-r3, 5.0 release3, v5.0.213, 163585 lines.
* 2024-04-03, [Release v5.0-r2](https://github.com/ossrs/srs/releases/tag/v5.0-r2), v5.0-r2, 5.0 release2, v5.0.210, 163515 lines.
* 2024-02-15, [Release v5.0-r1](https://github.com/ossrs/srs/releases/tag/v5.0-r1), v5.0-r1, 5.0 release1, v5.0.208, 163441 lines.
* 2023-12-30, [Release v5.0-r0](https://github.com/ossrs/srs/releases/tag/v5.0-r0), v5.0-r0, 5.0 release0, v5.0.205, 163363 lines.
* 2023-11-19, [Release v5.0-b7](https://github.com/ossrs/srs/releases/tag/v5.0-b7), v5.0-b7, 5.0 beta7, v5.0.200, 163305 lines.
* 2023-10-25, [Release v5.0-b6](https://github.com/ossrs/srs/releases/tag/v5.0-b6), v5.0-b6, 5.0 beta6, v5.0.195, 163303 lines.
* 2023-09-28, [Release v5.0-b5](https://github.com/ossrs/srs/releases/tag/v5.0-b5), v5.0-b5, 5.0 beta5, v5.0.185, 163254 lines.
* 2023-08-31, [Release v5.0-b4](https://github.com/ossrs/srs/releases/tag/v5.0-b4), v5.0-b4, 5.0 beta4, v5.0.176, 162919 lines.
* 2023-08-02, [Release v5.0-b3](https://github.com/ossrs/srs/releases/tag/v5.0-b3), v5.0-b3, 5.0 beta3, v5.0.170, 162704 lines.
* 2023-07-09, [Release v5.0-b2](https://github.com/ossrs/srs/releases/tag/v5.0-b2), v5.0-b2, 5.0 beta2, v5.0.166, 162520 lines.
* 2023-06-11, [Release v5.0-b1](https://github.com/ossrs/srs/releases/tag/v5.0-b1), v5.0-b1, 5.0 beta1, v5.0.157, 162494 lines.
* 2023-05-14, [Release v5.0-b0](https://github.com/ossrs/srs/releases/tag/v5.0-b0), v5.0-b0, 5.0 beta0, v5.0.155, 162600 lines.
* 2023-03-23, [Release v5.0-a5](https://github.com/ossrs/srs/releases/tag/v5.0-a5), v5.0-a5, 5.0 alpha5, v5.0.148, 162066 lines.
* 2023-02-12, [Release v5.0-a4](https://github.com/ossrs/srs/releases/tag/v5.0-a4), v5.0-a4, 5.0 alpha4, v5.0.141, 161897 lines.
* 2023-01-02, [Release v5.0-a3](https://github.com/ossrs/srs/releases/tag/v5.0-a3), v5.0-a3, 5.0 alpha3, v5.0.128, 161327 lines.
* 2022-12-18, [Release v5.0-a2](https://github.com/ossrs/srs/releases/tag/v5.0-a2), v5.0-a2, 5.0 alpha2, v5.0.112, 161233 lines.
* 2022-12-01, [Release v5.0-a1](https://github.com/ossrs/srs/releases/tag/v5.0-a1), v5.0-a1, 5.0 alpha1, v5.0.100, 160817 lines.
* 2022-11-25, [Release v5.0-a0](https://github.com/ossrs/srs/releases/tag/v5.0-a0), v5.0-a0, 5.0 alpha0, v5.0.98, 159813 lines.
* 2022-11-22, Release [v4.0-r4](https://github.com/ossrs/srs/releases/tag/v4.0-r4), v4.0-r4, 4.0 release4, v4.0.268, 145482 lines.
* 2022-09-16, Release [v4.0-r3](https://github.com/ossrs/srs/releases/tag/v4.0-r3), v4.0-r3, 4.0 release3, v4.0.265, 145328 lines.
* 2022-08-24, Release [v4.0-r2](https://github.com/ossrs/srs/releases/tag/v4.0-r2), v4.0-r2, 4.0 release2, v4.0.257, 144890 lines.
* 2022-06-29, Release [v4.0-r1](https://github.com/ossrs/srs/releases/tag/v4.0-r1), v4.0-r1, 4.0 release1, v4.0.253, 144680 lines.
* 2022-06-11, Release [v4.0-r0](https://github.com/ossrs/srs/releases/tag/v4.0-r0), v4.0-r0, 4.0 release0, v4.0.252, 144680 lines.
* 2020-06-27, [Release v3.0-r0](https://github.com/ossrs/srs/releases/tag/v3.0-r0), 3.0 release0, 3.0.141, 122674 lines.
* 2020-02-02, [Release v3.0-b0](https://github.com/ossrs/srs/releases/tag/v3.0-b0), 3.0 beta0, 3.0.112, 121709 lines.
* 2019-10-04, [Release v3.0-a0](https://github.com/ossrs/srs/releases/tag/v3.0-a0), 3.0 alpha0, 3.0.56, 107946 lines.
* 2017-03-03, [Release v2.0-r0](https://github.com/ossrs/srs/releases/tag/v2.0-r0), 2.0 release0, 2.0.234, 86373 lines.
* 2016-08-06, [Release v2.0-b0](https://github.com/ossrs/srs/releases/tag/v2.0-b0), 2.0 beta0, 2.0.210, 89704 lines.
* 2015-08-23, [Release v2.0-a0](https://github.com/ossrs/srs/releases/tag/v2.0-a0), 2.0 alpha0, 2.0.185, 89022 lines.
* 2014-12-05, [Release v1.0-r0](https://github.com/ossrs/srs/releases/tag/v1.0-r0), all bug fixed, 1.0.10, 59391 lines.
* 2014-10-09, [Release v0.9.8](https://github.com/ossrs/srs/releases/tag/v0.9.8), all bug fixed, 1.0.0, 59316 lines.
* 2014-04-07, [Release v0.9.1](https://github.com/ossrs/srs/releases/tag/v0.9.1), live streaming. 30000 lines.
* 2013-10-23, [Release v0.1.0](https://github.com/ossrs/srs/releases/tag/v0.1.0), rtmp. 8287 lines.
* 2013-10-17, Created.

## Features

Please read [FEATURES](trunk/doc/Features.md#features).

<a name="history"></a> <a name="change-logs"></a>
## Changelog

Please read [CHANGELOG](trunk/doc/CHANGELOG.md#changelog).

## Performance

Please read [PERFORMANCE](trunk/doc/PERFORMANCE.md#performance).

## Architecture

Please read [ARCHITECTURE](trunk/doc/Architecture.md#architecture).

## Ports

Please read [PORTS](trunk/doc/Resources.md#ports).

## APIs

Please read [APIS](trunk/doc/Resources.md#apis).

## Mirrors

Please read [MIRRORS](trunk/doc/Resources.md#mirrors).

## Dockers

Please read [DOCKERS](trunk/doc/Dockers.md).

Beijing, 2013.10<br/>
Winlin

