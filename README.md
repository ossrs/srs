# SRS(Simple Realtime Server)

![](http://ossrs.net/gif/v1/sls.gif?site=github.com&path=/srs/4.0release)
[![](https://github.com/ossrs/srs/actions/workflows/codeql-analysis.yml/badge.svg?branch=4.0release)](https://github.com/ossrs/srs/actions?query=workflow%3ACodeQL+branch%3A4.0release)
[![](https://github.com/ossrs/srs/actions/workflows/release.yml/badge.svg)](https://github.com/ossrs/srs/actions/workflows/release.yml?query=workflow%3ARelease)
[![](https://github.com/ossrs/srs/actions/workflows/test.yml/badge.svg?branch=4.0release)](https://github.com/ossrs/srs/actions?query=workflow%3ATest+branch%3A4.0release)
[![](https://codecov.io/gh/ossrs/srs/branch/4.0release/graph/badge.svg)](https://codecov.io/gh/ossrs/srs/branch/4.0release)
[![](https://ossrs.net/wiki/images/wechat-badge4.svg)](../../wikis/Contact#wechat)
[![](https://ossrs.net/wiki/images/srs-faq.svg)](https://github.com/ossrs/srs/issues/2716)
[![](https://ossrs.net/wiki/images/mulan-incubating.svg)](http://mulanos.cn)
[![](https://ossrs.net/wiki/images/srs-alternativeto.svg)](https://alternativeto.net/software/srs/about/)
[![](https://ossrs.net/wiki/images/srs-facebook.svg)](https://www.facebook.com/winlinvip)
[![](https://badgen.net/twitter/follow/winlinvip)](https://twitter.com/winlinvip)
[![](https://img.shields.io/youtube/channel/views/UCP6ZblCL_fIJoEyUzZxC1ng?style=social)](https://www.youtube.com/channel/UCP6ZblCL_fIJoEyUzZxC1ng)
[![](https://img.shields.io/twitch/status/winlinvip?style=social)](https://www.twitch.tv/winlinvip)
[![](https://badgen.net/discord/members/yZ4BnPmHAd)](https://discord.gg/yZ4BnPmHAd)
[![](https://opencollective.com/srs-server/tiers/badge.svg)](https://opencollective.com/srs-server/contribute)
[![](https://badgen.net/badge/srs/stackoverflow/orange?icon=terminal)](https://stackoverflow.com/questions/tagged/simple-realtime-server)
[![](https://img.shields.io/docker/pulls/ossrs/srs)](https://hub.docker.com/r/ossrs/srs/tags)

SRS/4.0，[Leo](https://github.com/ossrs/srs/wiki/Product#release40)，是一个简单高效的实时视频服务器，支持RTMP/WebRTC/HLS/HTTP-FLV/SRT。

SRS is a simple, high efficiency and realtime video server, supports RTMP/WebRTC/HLS/HTTP-FLV/SRT.

[![SRS Overview](https://ossrs.net/wiki/images/SRS-Overview-4.0.png?v=100)](https://ossrs.net/wiki/images/SRS-Overview-4.0.png)

> Note: Please see https://www.processon.com/view/link/619f29791efad425fd699fd2

SRS is licenced under [MIT][LICENSE], but some depended libraries are distributed using their [own licenses][LicenseMixing].

<a name="product"></a>
<a name="usage-docker"></a>
## Usage

> Highly recommend that directly run SRS by 
> **docker([CN](https://github.com/ossrs/srs/wiki/v4_CN_Home#docker) / [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home#docker))**, 
> or **K8s([CN](https://github.com/ossrs/srs/wiki/v4_CN_Home#k8s) / [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home#k8s))**, 
> however it's also easy to build SRS from source code, for detail please see 
> **Getting Started([CN](https://github.com/ossrs/srs/wiki/v4_CN_Home#getting-started) / [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home#getting-started))**.

Build SRS from source:

```
git clone -b 4.0release https://gitee.com/ossrs/srs.git &&
cd srs/trunk && ./configure && make && ./objs/srs -c conf/srs.conf
```

Open [http://localhost:8080/](http://localhost:8080/) to check it, then publish
by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) as:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

> Note: It's also able to [publish by H5](http://localhost:8080/players/rtc_publisher.html?autostart=true) if WebRTC is enabled.

Play the following streams by [players](https://ossrs.net):

* RTMP (by [VLC](https://www.videolan.org/)): rtmp://localhost/live/livestream
* H5(HTTP-FLV): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)
* H5(HLS): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8&port=8080&schema=http)

Note that if convert RTMP to WebRTC, please use [`rtmp2rtc.conf`](https://github.com/ossrs/srs/issues/2728#issuecomment-964686152):

* H5(WebRTC): [webrtc://localhost/live/livestream](http://localhost:8080/players/rtc_player.html?autostart=true)

<a name="srs-40-wiki"></a>
<a name="wiki"></a>

From here, please read wikis:

* [Getting Started](https://github.com/ossrs/srs/wiki/v4_EN_Home#getting-started), please read Wiki first.
* [中文文档：起步](https://github.com/ossrs/srs/wiki/v4_CN_Home#getting-started)，不读Wiki一定扑街，不读文档请不要提Issue，不读文档请不要提问题，任何文档中明确说过的疑问都不会解答。

Fast index for Wikis:

* Overview? ([CN][v4_CN_Home], [EN][v4_EN_Home])
* How to deliver RTMP streaming?([CN][v4_CN_SampleRTMP], [EN][v4_EN_SampleRTMP])
* How to build RTMP Edge-Cluster?([CN][v4_CN_SampleRTMPCluster], [EN][v4_EN_SampleRTMPCluster])
* How to build RTMP Origin-Cluster?([CN][v4_CN_SampleOriginCluster], [EN][v4_EN_SampleOriginCluster])
* How to deliver HTTP-FLV streaming?([CN][v4_CN_SampleHttpFlv], [EN][v4_EN_SampleHttpFlv])
* How to deliver HLS streaming?([CN][v4_CN_SampleHLS], [EN][v4_EN_SampleHLS])
* How to deliver low-latency streaming?([CN][v4_CN_SampleRealtime], [EN][v4_EN_SampleRealtime])
* How to use WebRTC? ([CN][v4_CN_WebRTC], [EN][v4_EN_WebRTC])

Other important wiki:

* Usage: How to delivery DASH(Experimental)?([CN][v4_CN_SampleDASH], [EN][v4_EN_SampleDASH])
* Usage: How to transode RTMP stream by FFMPEG?([CN][v4_CN_SampleFFMPEG], [EN][v4_EN_SampleFFMPEG])
* Usage: How to delivery HTTP FLV Live Streaming Cluster?([CN][v4_CN_SampleHttpFlvCluster], [EN][v4_EN_SampleHttpFlvCluster])
* Usage: How to ingest file/stream/device to RTMP?([CN][v4_CN_SampleIngest], [EN][v4_EN_SampleIngest])
* Usage: How to forward stream to other servers?([CN][v4_CN_SampleForward], [EN][v4_EN_SampleForward])
* Usage: How to improve edge performance for multiple CPUs? ([CN][v4_CN_REUSEPORT], [EN][v4_EN_REUSEPORT])
* Usage: How to file a bug or contact us? ([CN][v4_CN_Contact], [EN][v4_EN_Contact])

## AUTHORS

Thank you to all our contributors! 🙏

[![](https://opencollective.com/srs-server/contributors.svg?width=800&button=false)](https://opencollective.com/srs-server/contribute)

> Note: You may provide financial support for this project by donating [via Open Collective](https://opencollective.com/srs-server/contribute). Thank you for your support!

There are two types of people that have contributed code to the SRS project:

* Maintainers: Contribute and maintain important features. SRS always remembers and thanks you by writing your names in stream metadata.
* [Contributors][authors]: Submit patches, report bugs, add translations, help answer newbie questions, and generally make SRS much better.

Maintainers of SRS project:

* [Winlin](https://github.com/winlinvip): Focus on [issues/PR](https://github.com/ossrs/srs/issues) and tests now.
* [ZhaoWenjie](https://github.com/wenjiegit): Focus on [HDS](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_DeliveryHDS) module.
* [ShiWei](https://github.com/runner365): Focus on [SRT](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_SRTWiki) module.
* [XiaoZhihong](https://github.com/xiaozhihong): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.
* [WuPengqiang](https://github.com/Bepartofyou): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.
* [XiaLixin](https://github.com/xialixin): Focus on [GB28181](https://github.com/ossrs/srs/issues/1500) module.
* [LiPeng](https://github.com/lipeng19811218): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.
* [ChenGuanghua](https://github.com/chen-guanghua): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.
* [ChenHaibo](https://github.com/duiniuluantanqin): Focus on [GB28181](https://github.com/ossrs/srs/issues/1500) and [API](https://github.com/ossrs/srs/issues/1657) module.

A big THANK YOU goes to:

* All friends of SRS for [big supports][bigthanks].
* Genes amd Mabbott for creating [st][st]([state-threads][st2]).
* [Michael Talyanksy](https://github.com/michaeltalyansky) for introducing ST to us.

## Releases

* 2022-01-03, Release [v4.0-b2](https://github.com/ossrs/srs/releases/tag/v4.0-b2), v4.0-b2, 4.0 beta2, v4.0.215, 144278 lines.
* 2021-12-19, Release [v4.0-b1](https://github.com/ossrs/srs/releases/tag/v4.0-b1), v4.0-b1, 4.0 beta1, v4.0.206, 144126 lines.
* 2021-12-01, Release [v4.0-b0](https://github.com/ossrs/srs/releases/tag/v4.0-b0), v4.0-b0, 4.0 beta0, v4.0.201, 144022 lines.
* 2021-11-15, Release [v4.0.198](https://github.com/ossrs/srs/releases/tag/v4.0.198), 4.0 dev8, v4.0.198, 144010 lines.
* 2021-11-02, Release [v4.0.191](https://github.com/ossrs/srs/releases/tag/v4.0.191), 4.0 dev7, v4.0.191, 143890 lines.
* 2021-10-12, Release [v4.0.177](https://github.com/ossrs/srs/releases/tag/v4.0.177), 4.0 dev6, v4.0.177, 143686 lines.
* 2021-09-05, Release [v4.0.161](https://github.com/ossrs/srs/releases/tag/v4.0.161), 4.0 dev5, v4.0.161, 145865 lines.
* 2021-08-15, Release [v4.0.156](https://github.com/ossrs/srs/releases/tag/v4.0.156), 4.0 dev4, v4.0.156, 145490 lines.
* 2021-08-14, Release [v4.0.153](https://github.com/ossrs/srs/releases/tag/v4.0.153), 4.0 dev3, v4.0.153, 145506 lines.
* 2021-08-07, Release [v4.0.150](https://github.com/ossrs/srs/releases/tag/v4.0.150), 4.0 dev2, v4.0.150, 145289 lines.
* 2021-07-25, Release [v4.0.146](https://github.com/ossrs/srs/releases/tag/v4.0.146), 4.0 dev1, v4.0.146, 144026 lines.
* 2021-07-04, Release [v4.0.139](https://github.com/ossrs/srs/releases/tag/v4.0.139), 4.0 dev0, v4.0.139, 143245 lines.
* 2020-06-27, [Release v3.0-r0][r3.0r0], 3.0 release0, 3.0.141, 122674 lines.
* 2020-02-02, [Release v3.0-b0][r3.0b0], 3.0 beta0, 3.0.112, 121709 lines.
* 2019-10-04, [Release v3.0-a0][r3.0a0], 3.0 alpha0, 3.0.56, 107946 lines.
* 2017-03-03, [Release v2.0-r0][r2.0r0], 2.0 release0, 2.0.234, 86373 lines.
* 2016-08-06, [Release v2.0-b0][r2.0b0], 2.0 beta0, 2.0.210, 89704 lines.
* 2015-08-23, [Release v2.0-a0][r2.0a0], 2.0 alpha0, 2.0.185, 89022 lines.
* 2014-12-05, [Release v1.0-r0][r1.0r0], all bug fixed, 1.0.10, 59391 lines.
* 2014-10-09, [Release v0.9.8][r1.0b0], all bug fixed, 1.0.0, 59316 lines.
* 2014-04-07, [Release v0.9.1][r1.0a0], live streaming. 30000 lines.
* 2013-10-23, [Release v0.1.0][r0.1], rtmp. 8287 lines.
* 2013-10-17, Created.

## Features

Please read [FEATURES](trunk/doc/Features.md#features).

<a name="history"></a>
<a name="change-logs"></a>

## Changelog

Please read [CHANGELOG](trunk/doc/CHANGELOG.md#changelog).

## Compare

Comparing with other media servers, SRS is much better and stronger, for details please 
read Product([CN][v4_CN_Compare]/[EN][v4_EN_Compare]).

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

Beijing, 2013.10<br/>
Winlin

[authors]: https://github.com/ossrs/srs/blob/4.0release/trunk/AUTHORS.txt
[bigthanks]: https://github.com/ossrs/srs/wiki/Product#release40
[st]: https://github.com/ossrs/state-threads
[st2]: https://github.com/ossrs/state-threads/tree/srs
[state-threads]: https://github.com/ossrs/state-threads/tree/srs
[nginx]: http://nginx.org/
[srs]: https://github.com/ossrs/srs
[gitee]: https://gitee.com/ossrs/srs
[srs-bench]: https://github.com/ossrs/srs-bench
[srs-ngb]: https://github.com/ossrs/srs-console
[srs-librtmp]: https://github.com/ossrs/srs-librtmp
[gitlab]: https://gitlab.com/winlinvip/srs-gitlab
[console]: http://ossrs.net:8080/console
[docker-srs3]: https://github.com/ossrs/srs-docker/tree/v3#usage
[docker-srs4]: https://github.com/ossrs/srs-docker/tree/v4#usage
[docker-dev]: https://github.com/ossrs/srs-docker/tree/dev#usage

[v4_CN_Git]: https://github.com/ossrs/srs/wiki/v4_CN_Git
[v4_EN_Git]: https://github.com/ossrs/srs/wiki/v4_EN_Git
[v4_CN_SampleRTMP]: https://github.com/ossrs/srs/wiki/v4_CN_SampleRTMP
[v4_EN_SampleRTMP]: https://github.com/ossrs/srs/wiki/v4_EN_SampleRTMP
[v4_CN_SampleRTMPCluster]: https://github.com/ossrs/srs/wiki/v4_CN_SampleRTMPCluster
[v4_EN_SampleRTMPCluster]: https://github.com/ossrs/srs/wiki/v4_EN_SampleRTMPCluster
[v4_CN_SampleOriginCluster]: https://github.com/ossrs/srs/wiki/v4_CN_SampleOriginCluster
[v4_EN_SampleOriginCluster]: https://github.com/ossrs/srs/wiki/v4_EN_SampleOriginCluster
[v4_CN_SampleHLS]: https://github.com/ossrs/srs/wiki/v4_CN_SampleHLS
[v4_EN_SampleHLS]: https://github.com/ossrs/srs/wiki/v4_EN_SampleHLS
[v4_CN_SampleTranscode2HLS]: https://github.com/ossrs/srs/wiki/v4_CN_SampleTranscode2HLS
[v4_EN_SampleTranscode2HLS]: https://github.com/ossrs/srs/wiki/v4_EN_SampleTranscode2HLS
[v4_CN_SampleFFMPEG]: https://github.com/ossrs/srs/wiki/v4_CN_SampleFFMPEG
[v4_EN_SampleFFMPEG]: https://github.com/ossrs/srs/wiki/v4_EN_SampleFFMPEG
[v4_CN_SampleForward]: https://github.com/ossrs/srs/wiki/v4_CN_SampleForward
[v4_EN_SampleForward]: https://github.com/ossrs/srs/wiki/v4_EN_SampleForward
[v4_CN_SampleRealtime]: https://github.com/ossrs/srs/wiki/v4_CN_SampleRealtime
[v4_EN_SampleRealtime]: https://github.com/ossrs/srs/wiki/v4_EN_SampleRealtime
[v4_CN_WebRTC]: https://github.com/ossrs/srs/wiki/v4_CN_WebRTC
[v4_EN_WebRTC]: https://github.com/ossrs/srs/wiki/v4_EN_WebRTC
[v4_CN_WebRTC#config-candidate]: https://github.com/ossrs/srs/wiki/v4_CN_WebRTC#config-candidate
[v4_EN_WebRTC#config-candidate]: https://github.com/ossrs/srs/wiki/v4_EN_WebRTC#config-candidate
[v4_CN_SampleARM]: https://github.com/ossrs/srs/wiki/v4_CN_SampleARM
[v4_EN_SampleARM]: https://github.com/ossrs/srs/wiki/v4_EN_SampleARM
[v4_CN_SampleIngest]: https://github.com/ossrs/srs/wiki/v4_CN_SampleIngest
[v4_EN_SampleIngest]: https://github.com/ossrs/srs/wiki/v4_EN_SampleIngest
[v4_CN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v4_CN_SampleHTTP
[v4_EN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v4_EN_SampleHTTP
[v4_CN_SampleDemo]: https://github.com/ossrs/srs/wiki/v4_CN_SampleDemo
[v4_EN_SampleDemo]: https://github.com/ossrs/srs/wiki/v4_EN_SampleDemo
[v4_CN_OriginCluster]: https://github.com/ossrs/srs/wiki/v4_CN_OriginCluster
[v4_EN_OriginCluster]: https://github.com/ossrs/srs/wiki/v4_EN_OriginCluster
[v4_CN_REUSEPORT]: https://github.com/ossrs/srs/wiki/v4_CN_REUSEPORT
[v4_EN_REUSEPORT]: https://github.com/ossrs/srs/wiki/v4_EN_REUSEPORT
[v4_CN_Sample]: https://github.com/ossrs/srs/wiki/v4_CN_Sample
[v4_EN_Sample]: https://github.com/ossrs/srs/wiki/v4_EN_Sample
[v4_CN_Product]: https://github.com/ossrs/srs/wiki/v4_CN_Product
[v4_EN_Product]: https://github.com/ossrs/srs/wiki/v4_EN_Product
[v4_CN_Home]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[v4_EN_Home]: https://github.com/ossrs/srs/wiki/v4_EN_Home

[v4_CN_Compare]: https://github.com/ossrs/srs/wiki/v4_CN_Compare
[v4_EN_Compare]: https://github.com/ossrs/srs/wiki/v4_EN_Compare
[v4_CN_Build]: https://github.com/ossrs/srs/wiki/v4_CN_Build
[v4_EN_Build]: https://github.com/ossrs/srs/wiki/v4_EN_Build
[v4_CN_Performance]: https://github.com/ossrs/srs/wiki/v4_CN_Performance
[v4_EN_Performance]: https://github.com/ossrs/srs/wiki/v4_EN_Performance
[v4_CN_DeliveryRTMP]: https://github.com/ossrs/srs/wiki/v4_CN_DeliveryRTMP
[v4_EN_DeliveryRTMP]: https://github.com/ossrs/srs/wiki/v4_EN_DeliveryRTMP
[v4_CN_Edge]: https://github.com/ossrs/srs/wiki/v4_CN_Edge
[v4_EN_Edge]: https://github.com/ossrs/srs/wiki/v4_EN_Edge
[v4_CN_RtmpUrlVhost]: https://github.com/ossrs/srs/wiki/v4_CN_RtmpUrlVhost
[v4_EN_RtmpUrlVhost]: https://github.com/ossrs/srs/wiki/v4_EN_RtmpUrlVhost
[v4_CN_RTMPHandshake]: https://github.com/ossrs/srs/wiki/v4_CN_RTMPHandshake
[v4_EN_RTMPHandshake]: https://github.com/ossrs/srs/wiki/v4_EN_RTMPHandshake
[v4_CN_HTTPServer]: https://github.com/ossrs/srs/wiki/v4_CN_HTTPServer
[v4_EN_HTTPServer]: https://github.com/ossrs/srs/wiki/v4_EN_HTTPServer
[v4_CN_DeliveryHLS]: https://github.com/ossrs/srs/wiki/v4_CN_DeliveryHLS
[v4_EN_DeliveryHLS]: https://github.com/ossrs/srs/wiki/v4_EN_DeliveryHLS
[v4_CN_DeliveryHLS2]: https://github.com/ossrs/srs/wiki/v4_CN_DeliveryHLS#hlsaudioonly
[v4_EN_DeliveryHLS2]: https://github.com/ossrs/srs/wiki/v4_EN_DeliveryHLS#hlsaudioonly
[v4_CN_Reload]: https://github.com/ossrs/srs/wiki/v4_CN_Reload
[v4_EN_Reload]: https://github.com/ossrs/srs/wiki/v4_EN_Reload
[v4_CN_LowLatency2]: https://github.com/ossrs/srs/wiki/v4_CN_LowLatency#gop-cache
[v4_EN_LowLatency2]: https://github.com/ossrs/srs/wiki/v4_EN_LowLatency#gop-cache
[v4_CN_Forward]: https://github.com/ossrs/srs/wiki/v4_CN_Forward
[v4_EN_Forward]: https://github.com/ossrs/srs/wiki/v4_EN_Forward
[v4_CN_FFMPEG]: https://github.com/ossrs/srs/wiki/v4_CN_FFMPEG
[v4_EN_FFMPEG]: https://github.com/ossrs/srs/wiki/v4_EN_FFMPEG
[v4_CN_HTTPCallback]: https://github.com/ossrs/srs/wiki/v4_CN_HTTPCallback
[v4_EN_HTTPCallback]: https://github.com/ossrs/srs/wiki/v4_EN_HTTPCallback
[v4_CN_SampleDemo]: https://github.com/ossrs/srs/wiki/v4_CN_SampleDemo
[v4_EN_SampleDemo]: https://github.com/ossrs/srs/wiki/v4_EN_SampleDemo
[v4_CN_SrsLinuxArm]: https://github.com/ossrs/srs/wiki/v4_CN_SrsLinuxArm
[v4_EN_SrsLinuxArm]: https://github.com/ossrs/srs/wiki/v4_EN_SrsLinuxArm
[v4_CN_LinuxService]: https://github.com/ossrs/srs/wiki/v4_CN_LinuxService
[v4_EN_LinuxService]: https://github.com/ossrs/srs/wiki/v4_EN_LinuxService
[v4_CN_RTMP-ATC]: https://github.com/ossrs/srs/wiki/v4_CN_RTMP-ATC
[v4_EN_RTMP-ATC]: https://github.com/ossrs/srs/wiki/v4_EN_RTMP-ATC
[v4_CN_HTTPApi]: https://github.com/ossrs/srs/wiki/v4_CN_HTTPApi
[v4_EN_HTTPApi]: https://github.com/ossrs/srs/wiki/v4_EN_HTTPApi
[v4_CN_Ingest]: https://github.com/ossrs/srs/wiki/v4_CN_Ingest
[v4_EN_Ingest]: https://github.com/ossrs/srs/wiki/v4_EN_Ingest
[v4_CN_DVR]: https://github.com/ossrs/srs/wiki/v4_CN_DVR
[v4_EN_DVR]: https://github.com/ossrs/srs/wiki/v4_EN_DVR
[v4_CN_SrsLog]: https://github.com/ossrs/srs/wiki/v4_CN_SrsLog
[v4_EN_SrsLog]: https://github.com/ossrs/srs/wiki/v4_EN_SrsLog
[v4_CN_DRM2]: https://github.com/ossrs/srs/wiki/v4_CN_DRM#tokentraverse
[v4_EN_DRM2]: https://github.com/ossrs/srs/wiki/v4_EN_DRM#tokentraverse
[v4_CN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v4_CN_SampleHTTP
[v4_EN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v4_EN_SampleHTTP
[v4_CN_FlvVodStream]: https://github.com/ossrs/srs/wiki/v4_CN_FlvVodStream
[v4_EN_FlvVodStream]: https://github.com/ossrs/srs/wiki/v4_EN_FlvVodStream
[v4_CN_Security]: https://github.com/ossrs/srs/wiki/v4_CN_Security
[v4_EN_Security]: https://github.com/ossrs/srs/wiki/v4_EN_Security
[v4_CN_DeliveryHttpStream]: https://github.com/ossrs/srs/wiki/v4_CN_DeliveryHttpStream
[v4_EN_DeliveryHttpStream]: https://github.com/ossrs/srs/wiki/v4_EN_DeliveryHttpStream
[v4_CN_DeliveryHDS]: https://github.com/ossrs/srs/wiki/v4_CN_DeliveryHDS
[v4_EN_DeliveryHDS]: https://github.com/ossrs/srs/wiki/v4_EN_DeliveryHDS
[v4_CN_Streamer]: https://github.com/ossrs/srs/wiki/v4_CN_Streamer
[v4_EN_Streamer]: https://github.com/ossrs/srs/wiki/v4_EN_Streamer
[v4_CN_Streamer2]: https://github.com/ossrs/srs/wiki/v4_CN_Streamer#push-http-flv-to-srs
[v4_EN_Streamer2]: https://github.com/ossrs/srs/wiki/v4_EN_Streamer#push-http-flv-to-srs
[v4_CN_SampleHttpFlv]: https://github.com/ossrs/srs/wiki/v4_CN_SampleHttpFlv
[v4_EN_SampleHttpFlv]: https://github.com/ossrs/srs/wiki/v4_EN_SampleHttpFlv
[v4_CN_SampleHttpFlvCluster]: https://github.com/ossrs/srs/wiki/v4_CN_SampleHttpFlvCluster
[v4_EN_SampleHttpFlvCluster]: https://github.com/ossrs/srs/wiki/v4_EN_SampleHttpFlvCluster
[v4_CN_SampleDASH]:https://github.com/ossrs/srs/wiki/v4_CN_SampleDASH
[v4_EN_SampleDASH]:https://github.com/ossrs/srs/wiki/v4_EN_SampleDASH

[bug #547]: https://github.com/ossrs/srs/issues/547
[bug #301]: https://github.com/ossrs/srs/issues/301
[bug #459]: https://github.com/ossrs/srs/issues/459
[bug #367]: https://github.com/ossrs/srs/issues/367
[bug #257]: https://github.com/ossrs/srs/issues/257
[bug #904]: https://github.com/ossrs/srs/issues/904
[bug #913]: https://github.com/ossrs/srs/issues/913
[bug #1059]: https://github.com/ossrs/srs/issues/1059
[bug #92]: https://github.com/ossrs/srs/issues/92
[bug #464]: https://github.com/ossrs/srs/issues/464
[bug #460]: https://github.com/ossrs/srs/issues/460
[bug #775]: https://github.com/ossrs/srs/issues/775
[bug #307]: https://github.com/ossrs/srs/issues/307
[bug #2324]: https://github.com/ossrs/srs/issues/2324
[bug #1657]: https://github.com/ossrs/srs/issues/1657
[bug #1500]: https://github.com/ossrs/srs/issues/1500
[bug #738]: https://github.com/ossrs/srs/issues/738
[bug #299]: https://github.com/ossrs/srs/issues/299
[bug #250]: https://github.com/ossrs/srs/issues/250
[bug #459]: https://github.com/ossrs/srs/issues/459
[bug #470]: https://github.com/ossrs/srs/issues/470
[bug #319]: https://github.com/ossrs/srs/issues/319
[bug #1147]: https://github.com/ossrs/srs/issues/1147
[bug #2304]: https://github.com/ossrs/srs/issues/2304
[bug #1535]: https://github.com/ossrs/srs/issues/1535
[bug #1342]: https://github.com/ossrs/srs/issues/1342
[bug #1193]: https://github.com/ossrs/srs/issues/1193
[bug #2093]: https://github.com/ossrs/srs/issues/2093
[bug #465]: https://github.com/ossrs/srs/issues/465
[bug #2091]: https://github.com/ossrs/srs/issues/2091
[bug #2188]: https://github.com/ossrs/srs/issues/2188
[bug #413]: https://github.com/ossrs/srs/issues/413
[bug #1111]: https://github.com/ossrs/srs/issues/1111
[bug #463]: https://github.com/ossrs/srs/issues/463
[bug #775]: https://github.com/ossrs/srs/issues/775
[bug #257-c0]: https://github.com/ossrs/srs/issues/257#issuecomment-66864413

[r3.0r5]: https://github.com/ossrs/srs/releases/tag/v3.0-r5
[r3.0r4]: https://github.com/ossrs/srs/releases/tag/v3.0-r4
[r3.0r3]: https://github.com/ossrs/srs/releases/tag/v3.0-r3
[r3.0r2]: https://github.com/ossrs/srs/releases/tag/v3.0-r2
[r3.0r1]: https://github.com/ossrs/srs/releases/tag/v3.0-r1
[r3.0r0]: https://github.com/ossrs/srs/releases/tag/v3.0-r0
[r3.0b4]: https://github.com/ossrs/srs/releases/tag/v3.0-b4
[r3.0b3]: https://github.com/ossrs/srs/releases/tag/v3.0-b3
[r3.0b2]: https://github.com/ossrs/srs/releases/tag/v3.0-b2
[r3.0b1]: https://github.com/ossrs/srs/releases/tag/v3.0-b1
[r3.0b0]: https://github.com/ossrs/srs/releases/tag/v3.0-b0
[r3.0a9]: https://github.com/ossrs/srs/releases/tag/v3.0-a9
[r3.0a8]: https://github.com/ossrs/srs/releases/tag/v3.0-a8
[r3.0a7]: https://github.com/ossrs/srs/releases/tag/v3.0-a7
[r3.0a6]: https://github.com/ossrs/srs/releases/tag/v3.0-a6
[r3.0a5]: https://github.com/ossrs/srs/releases/tag/v3.0-a5
[r3.0a4]: https://github.com/ossrs/srs/releases/tag/v3.0-a4
[r3.0a3]: https://github.com/ossrs/srs/releases/tag/v3.0-a3
[r3.0a2]: https://github.com/ossrs/srs/releases/tag/v3.0-a2
[r3.0a1]: https://github.com/ossrs/srs/releases/tag/v3.0-a1
[r3.0a0]: https://github.com/ossrs/srs/releases/tag/v3.0-a0
[r2.0r8]: https://github.com/ossrs/srs/releases/tag/v2.0-r8
[r2.0r7]: https://github.com/ossrs/srs/releases/tag/v2.0-r7
[r2.0r6]: https://github.com/ossrs/srs/releases/tag/v2.0-r6
[r2.0r5]: https://github.com/ossrs/srs/releases/tag/v2.0-r5
[r2.0r4]: https://github.com/ossrs/srs/releases/tag/v2.0-r4
[r2.0r3]: https://github.com/ossrs/srs/releases/tag/v2.0-r3
[r2.0r2]: https://github.com/ossrs/srs/releases/tag/v2.0-r2
[r2.0r1]: https://github.com/ossrs/srs/releases/tag/v2.0-r1
[r2.0r0]: https://github.com/ossrs/srs/releases/tag/v2.0-r0
[r2.0b4]: https://github.com/ossrs/srs/releases/tag/v2.0-b4
[r2.0b3]: https://github.com/ossrs/srs/releases/tag/v2.0-b3
[r2.0b2]: https://github.com/ossrs/srs/releases/tag/v2.0-b2
[r2.0b1]: https://github.com/ossrs/srs/releases/tag/v2.0-b1
[r2.0b0]: https://github.com/ossrs/srs/releases/tag/v2.0-b0
[r2.0a3]: https://github.com/ossrs/srs/releases/tag/v2.0-a3
[r2.0a2]: https://github.com/ossrs/srs/releases/tag/v2.0-a2
[r2.0a1]: https://github.com/ossrs/srs/releases/tag/v2.0-a1
[r2.0a0]: https://github.com/ossrs/srs/releases/tag/v2.0-a0
[r1.0r4]: https://github.com/ossrs/srs/releases/tag/v1.0-r4
[r1.0r3]: https://github.com/ossrs/srs/releases/tag/v1.0-r3
[r1.0r2]: https://github.com/ossrs/srs/releases/tag/v1.0-r2
[r1.0r1]: https://github.com/ossrs/srs/releases/tag/v1.0-r1
[r1.0r0]: https://github.com/ossrs/srs/releases/tag/v1.0-r0
[r1.0b0]: https://github.com/ossrs/srs/releases/tag/v0.9.8
[r1.0a7]: https://github.com/ossrs/srs/releases/tag/v0.9.7
[r1.0a6]: https://github.com/ossrs/srs/releases/tag/v0.9.6
[r1.0a5]: https://github.com/ossrs/srs/releases/tag/v0.9.5
[r1.0a4]: https://github.com/ossrs/srs/releases/tag/v0.9.4
[r1.0a3]: https://github.com/ossrs/srs/releases/tag/v0.9.3
[r1.0a2]: https://github.com/ossrs/srs/releases/tag/v0.9.2
[r1.0a0]: https://github.com/ossrs/srs/releases/tag/v0.9.1
[r0.9]: https://github.com/ossrs/srs/releases/tag/v0.9.0
[r0.8]: https://github.com/ossrs/srs/releases/tag/v0.8.0
[r0.7]: https://github.com/ossrs/srs/releases/tag/v0.7.0
[r0.6]: https://github.com/ossrs/srs/releases/tag/v0.6.0
[r0.5]: https://github.com/ossrs/srs/releases/tag/v0.5.0
[r0.4]: https://github.com/ossrs/srs/releases/tag/v0.4.0
[r0.3]: https://github.com/ossrs/srs/releases/tag/v0.3.0
[r0.2]: https://github.com/ossrs/srs/releases/tag/v0.2.0
[r0.1]: https://github.com/ossrs/srs/releases/tag/v0.1.0


[v4_CN_Contact]: https://github.com/ossrs/srs/wiki/v4_CN_Contact
[v4_EN_Contact]: https://github.com/ossrs/srs/wiki/v4_EN_Contact

[LICENSE]: https://github.com/ossrs/srs/blob/4.0release/LICENSE
[LicenseMixing]: https://github.com/ossrs/srs/wiki/LicenseMixing

