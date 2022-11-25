# SRS(Simple Realtime Server)

![](http://ossrs.net/gif/v1/sls.gif?site=github.com&path=/srs/develop)
[![](https://github.com/ossrs/srs/actions/workflows/codeql-analysis.yml/badge.svg?branch=develop)](https://github.com/ossrs/srs/actions?query=workflow%3ACodeQL+branch%3Adevelop)
[![](https://github.com/ossrs/srs/actions/workflows/release.yml/badge.svg)](https://github.com/ossrs/srs/actions/workflows/release.yml?query=workflow%3ARelease)
[![](https://github.com/ossrs/srs/actions/workflows/test.yml/badge.svg?branch=develop)](https://github.com/ossrs/srs/actions?query=workflow%3ATest+branch%3Adevelop)
[![](https://codecov.io/gh/ossrs/srs/branch/develop/graph/badge.svg?token=Zx2LhdtA39)](https://app.codecov.io/gh/ossrs/srs/tree/develop)
[![](https://ossrs.net/wiki/images/wechat-badge4.svg)](https://ossrs.net/lts/zh-cn/contact#discussion)
[![](https://img.shields.io/twitter/follow/srs_server?style=social)](https://twitter.com/srs_server)
[![](https://badgen.net/discord/members/yZ4BnPmHAd)](https://discord.gg/yZ4BnPmHAd)
[![](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fossrs%2Fsrs.svg?type=small)](https://app.fossa.com/projects/git%2Bgithub.com%2Fossrs%2Fsrs?ref=badge_small)
[![](https://ossrs.net/wiki/images/srs-faq.svg)](https://ossrs.net/lts/zh-cn/faq)
[![](https://badgen.net/badge/srs/stackoverflow/orange?icon=terminal)](https://stackoverflow.com/questions/tagged/simple-realtime-server)
[![](https://opencollective.com/srs-server/tiers/badge.svg)](https://opencollective.com/srs-server/contribute)
[![](https://img.shields.io/docker/pulls/ossrs/srs)](https://hub.docker.com/r/ossrs/srs/tags)
[![](https://ossrs.net/wiki/images/do-btn-srs-125x20.svg)](https://cloud.digitalocean.com/droplets/new?appId=104916642&size=s-1vcpu-1gb&region=sgp1&image=ossrs-srs&type=applications)

SRS/6.0 is a simple, high efficiency and realtime video server, supports RTMP/WebRTC/HLS/HTTP-FLV/SRT/MPEG-DASH/GB28181, Linux/Windows/macOS, X86_64/ARMv7/AARCH64/M1/RISCV/LOONGARCH/MIPS, and essential [features](trunk/doc/Features.md#features).

[![SRS Overview](https://ossrs.net/wiki/images/SRS-SingleNode-4.0-sd.png?v=114)](https://ossrs.net/wiki/images/SRS-SingleNode-4.0-hd.png)

> Note:  The single node architecture for SRS, for detail please see [here](https://www.figma.com/file/333POxVznQ8Wz1Rxlppn36/SRS-4.0-Server-Arch).

SRS is licenced under [MIT](https://github.com/ossrs/srs/blob/develop/LICENSE) or [MulanPSL-2.0](https://spdx.org/licenses/MulanPSL-2.0.html),
and note that [MulanPSL-2.0 is compatible with Apache-2.0](https://www.apache.org/legal/resolved.html#category-a),
but some third-party libraries are distributed using their [own licenses](https://ossrs.io/lts/en-us/license).

<a name="product"></a> <a name="usage-docker"></a>
## Usage

Please read guide [Getting Started](https://ossrs.io/lts/en-us/docs/v4/doc/getting-started) or [中文文档：起步](https://ossrs.net/lts/zh-cn/docs/v4/doc/getting-started).

To build SRS from source:

```
git clone -b develop https://gitee.com/ossrs/srs.git &&
cd srs/trunk && ./configure && make && ./objs/srs -c conf/srs.conf
```

Open [http://localhost:8080/](http://localhost:8080/) to check it, then publish
by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) as:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

Play the following streams by [players](https://ossrs.net):

* RTMP (by [VLC](https://www.videolan.org/)): rtmp://localhost/live/livestream
* H5(HTTP-FLV): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)
* H5(HLS): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8&port=8080&schema=http)

Note that if convert RTMP to WebRTC, please use [`rtmp2rtc.conf`](https://github.com/ossrs/srs/issues/2728#issuecomment-964686152):

* H5(WebRTC): [webrtc://localhost/live/livestream](http://localhost:8080/players/rtc_player.html?autostart=true)

> Note: Besides of FFmpeg or OBS, it's also able to [publish by H5](http://localhost:8080/players/rtc_publisher.html?autostart=true) 
> if **WebRTC([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/webrtc#rtc-to-rtmp), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/webrtc#rtc-to-rtmp))** is enabled,
> please remember to set the **CANDIDATE([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/webrtc#config-candidate) or [EN](https://ossrs.io/lts/en-us/docs/v4/doc/webrtc#config-candidate))** for WebRTC.

> Highly recommend that directly run SRS by
> **docker([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/getting-started) / [EN](https://ossrs.io/lts/en-us/docs/v4/doc/getting-started))**,
> **Cloud Virtual Machine([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/getting-started-cloud) / [EN](https://ossrs.io/lts/en-us/docs/v4/doc/getting-started-cloud))**,
> or **K8s([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/getting-started-k8s) / [EN](https://ossrs.io/lts/en-us/docs/v4/doc/getting-started-k8s))**,
> however it's also easy to build SRS from source code, for detail please see
> **Getting Started([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/getting-started) / [EN](https://ossrs.io/lts/en-us/docs/v4/doc/getting-started))**.

> Note: If need HTTPS, by which WebRTC and modern browsers require, please read
> **HTTPS API([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/http-api#https-api) / [EN](https://ossrs.io/lts/en-us/docs/v4/doc/http-api#https-api))**
> and **HTTPS Callback([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/http-callback#https-callback) / [EN](https://ossrs.io/lts/en-us/docs/v4/doc/http-callback#https-callback))**
> and **HTTPS Live Streaming([CN](https://ossrs.io/lts/en-us/docs/v4/doc/delivery-http-flv#https-flv-live-stream) / [EN](https://ossrs.io/lts/en-us/docs/v4/doc/delivery-http-flv#https-flv-live-stream))**,
> however HTTPS proxy also works perfect with SRS such as Nginx.

<a name="srs-40-wiki"></a> <a name="wiki"></a>
From here, please read wikis:

* How to deliver RTMP streaming?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-rtmp), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-rtmp))
* How to deliver WebRTC streaming? ([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/webrtc), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/webrtc))
* How to covert RTMP to HTTP-FLV streaming?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-http-flv), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-http-flv))
* How to covert RTMP to HLS streaming?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-hls), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-hls))
* How to deliver low-latency streaming?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-realtime), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-realtime))
* How to build RTMP Edge-Cluster?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-rtmp-cluster), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-rtmp-cluster))
* How to build RTMP Origin-Cluster?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-origin-cluster), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-origin-cluster))
* How to build HLS Edge-Cluster?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-hls-cluster), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-hls-cluster))

Other important wiki:

* Usage: How to deliver DASH(Experimental)?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-dash), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-dash))
* Usage: How to transode RTMP stream by FFMPEG?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-ffmpeg), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-ffmpeg))
* Usage: How to deliver HTTP FLV Live Streaming Cluster?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-http-flvCluster), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-http-flvCluster))
* Usage: How to deliver HLS by NGINX Cluster?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-hls-cluster), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-hls-cluster))
* Usage: How to ingest file/stream/device to RTMP?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-ingest), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-ingest))
* Usage: How to forward stream to other servers?([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-forward), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-forward))
* Usage: How to improve edge performance for multiple CPUs? ([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/reuse-port), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/reuse-port))
* Usage: How to file a bug or contact us? ([CN](https://ossrs.net/lts/zh-cn/contact), [EN](https://ossrs.io/lts/en-us/contact))

## AUTHORS

Thank you to all our contributors! 🙏

[![](https://opencollective.com/srs-server/contributors.svg?width=800&button=false)](https://opencollective.com/srs-server/contribute)

> Note: You may provide financial support for this project by donating [via Open Collective](https://opencollective.com/srs-server/contribute). Thank you for your support!

The [TOC(Technical Oversight Committee)](trunk/AUTHORS.md#toc), [Developers](trunk/AUTHORS.md#developers) and [contributors](trunk/AUTHORS.md#contributors):

* [Winlin](https://github.com/winlinvip): Focus on [ST](https://github.com/ossrs/state-threads) and [Issues/PR](https://github.com/ossrs/srs/issues).
* [ZhaoWenjie](https://github.com/wenjiegit): Focus on [HDS](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_DeliveryHDS) and [Windows](https://github.com/ossrs/srs/issues/2532).
* [ShiWei](https://github.com/runner365): Focus on [SRT](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_SRTWiki) and [H.265](https://github.com/ossrs/srs/issues/465).
* [XiaoZhihong](https://github.com/xiaozhihong): Focus on [WebRTC/QUIC](https://github.com/ossrs/srs/issues/2091) and [SRT](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_SRTWiki).
* [WuPengqiang](https://github.com/Bepartofyou): Focus on [H.265](https://github.com/ossrs/srs/issues/465).
* [XiaLixin](https://github.com/xialixin): Focus on [GB28181](https://github.com/ossrs/srs/issues/1500).
* [LiPeng](https://github.com/lipeng19811218): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC).
* [ChenGuanghua](https://github.com/chen-guanghua): Focus on [WebRTC/QoS](https://github.com/ossrs/srs/issues/2051).
* [ChenHaibo](https://github.com/duiniuluantanqin): Focus on [GB28181](https://github.com/ossrs/srs/issues/1500) and [API](https://github.com/ossrs/srs/issues/1657).

A big `THANK YOU` also goes to:

* All [contributors](trunk/AUTHORS.md#contributors) of SRS.
* All friends of SRS for [big supports](https://ossrs.net/lts/zh-cn/product).
* [Genes](http://sourceforge.net/users/genes), [Mabbott](http://sourceforge.net/users/mabbott) and [Michael Talyanksy](https://github.com/michaeltalyansky) for creating and introducing [st](https://github.com/ossrs/state-threads/tree/srs).

## Contributing

We are grateful to the community for contributing bugfix and improvements, please follow the
[guide](https://github.com/ossrs/srs/contribute).

## LICENSE

[![FOSSA Status](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fossrs%2Fsrs.svg?type=small)](https://app.fossa.com/projects/git%2Bgithub.com%2Fossrs%2Fsrs?ref=badge_small)

SRS is licenced under [MIT](https://github.com/ossrs/srs/blob/develop/LICENSE) or [MulanPSL-2.0](https://spdx.org/licenses/MulanPSL-2.0.html),
and note that [MulanPSL-2.0 is compatible with Apache-2.0](https://www.apache.org/legal/resolved.html#category-a),
but some third-party libraries are distributed using their [own licenses](https://ossrs.net/lts/zh-cn/license).

[![](https://app.fossa.com/api/projects/git%2Bgithub.com%2Fossrs%2Fsrs.svg?type=large)](https://app.fossa.com/projects/git%2Bgithub.com%2Fossrs%2Fsrs?ref=badge_large)

## Releases

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

