#Simple-RTMP-Server

SRS/3.0，开发代号：[OuXuli](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Product#release30)

SRS定位是运营级的互联网直播服务器集群，追求更好的概念完整性和最简单实现的代码。<br/>
SRS is industrial-strength live streaming cluster, for the best conceptual integrity and the simplest implementation. 

Download from github.io: [Centos6-x86_64][centos0], [more...][more0]<br/>
Download from ossrs.net: [Centos6-x86_64][centos1], [more...][more1]<br/>
Contact by QQ or Skype, read [Contact][contact]

## Why SRS?

1. Completely rewrite HLS following m3u8/ts spec, and HLS support h.264+aac/mp3.
1. High efficient RTMP deliverying support 7k+ concurrency, vhost based, both origin and edge.
1. Embeded simplified media HTTP server for HLS, api and HTTP flv/ts/mp3/aac streaming.
1. Variety input: RTMP, pull by ingest file or stream(HTTP/RTMP/RTSP), push by stream caster 
RTSP/MPEGTS-over-UDP.
1. Popular internet delivery: RTMP/HDS for flash, HLS for mobile(IOS/IPad/MAC/Android), HTTP 
flv/ts/mp3/aac streaming for user prefered.
1. Enhanced DVR and hstrs: segment/session/append plan, customer path and HTTP callback.
the hstrs(http stream trigger rtmp source) enable the http-flv stream standby util encoder 
start publish, similar to rtmp, which will trigger edge to fetch from origin.
1. Multiple feature: transcode, forward, ingest, http hooks, dvr, hls, rtsp, http streaming, 
http api, refer, log, bandwith test and srs-librtmp.
1. Best maintainess: simple arch over state-threads(coroutine), single thread, single process 
and for linux/osx platform, common server x86-64/i386/arm/mips cpus, rich comments, strictly 
follows RTMP/HLS/RTSP spec.
1. Easy to use: both English and Chinese wiki, typically config files in trunk/conf, traceable 
and session based log, linux service script and install script.
1. MIT license, open source with product management and evolution.

Enjoy it!

## About

SRS(SIMPLE RTMP Server) over state-threads created in 2013.10.

SRS delivers rtmp/hls/http/hds live on x86/x64/arm/mips linux/osx, 
supports origin/edge/vhost and transcode/ingest and dvr/forward 
and http-api/http-callback/reload, introduces tracable 
session-oriented log, exports client srs-librtmp, 
with stream caster to push MPEGTS-over-UDP/RTSP to SRS,
provides EN/CN wiki and the most simple architecture.

## AUTHORS

There are three types of people that have contributed to the SRS project:
* AUTHORS: Contribute important features. Names of all 
PRIMARY response in NetConnection.connect and metadata. 
* CONTRIBUTORS: Submit patches, report bugs, add translations, help answer 
newbie questions, and generally make SRS that much better.

About all PRIMARY, AUTHORS and CONTRIBUTORS, read [AUTHORS.txt][authors].

A big THANK YOU goes to:
* All friends of SRS for [big supports][bigthanks].
* Genes amd Mabbott for creating [st][st]([state-threads][st2]).
* Michael Talyanksy for introducing us to use st.
* Roman Arutyunyan for creating [nginx-rtmp][nginx-rtmp] for SRS to refer to. 
* Joyent for creating [http-parser][http-parser] for http-api for SRS.
* Igor Sysoev for creating [nginx][nginx] for SRS to refer to.
* [FFMPEG][FFMPEG] and [libx264][libx264] group for SRS to use to transcode.
* Guido van Rossum for creating Python for api-server for SRS.

## Mirrors

Github: [https://github.com/simple-rtmp-server/srs][srs], the GIT usage([CN][v1_CN_Git], [EN][v1_EN_Git])

```
git clone https://github.com/simple-rtmp-server/srs.git
```

CSDN: [https://code.csdn.net/winlinvip/srs-csdn][csdn], the GIT usage([CN][v1_CN_Git], [EN][v1_EN_Git])

```
git clone https://code.csdn.net/winlinvip/srs-csdn.git
```

OSChina: [http://git.oschina.net/winlinvip/srs.oschina][oschina], the GIT usage([CN][v1_CN_Git], [EN][v1_EN_Git])

```
git clone https://git.oschina.net/winlinvip/srs.oschina.git
```

Gitlab: [https://gitlab.com/winlinvip/srs-gitlab][gitlab], the GIT usage([CN][v1_CN_Git], [EN][v1_EN_Git])

```
git clone https://gitlab.com/winlinvip/srs-gitlab.git
```

## Usage

<strong>Step 1:</strong> get SRS 

<pre>
git clone https://github.com/simple-rtmp-server/srs &&
cd simple-rtmp-server/trunk
</pre>

<strong>Step 2:</strong> build SRS,
<strong>Requires Centos6.x/Ubuntu12 32/64bits, others see Build(
[CN](https://github.com/simple-rtmp-server/srs/wiki/v2_CN_Build),
[EN](https://github.com/simple-rtmp-server/srs/wiki/v2_EN_Build)
).</strong>

<pre>
./configure && make
</pre>

<strong>Step 3:</strong> start SRS 

<pre>
./objs/srs -c conf/srs.conf
</pre>

<strong>See also:</strong>
* Usage: How to delivery RTMP?([CN][v1_CN_SampleRTMP], [EN][v1_EN_SampleRTMP])
* Usage: How to delivery RTMP Cluster?([CN][v1_CN_SampleRTMPCluster], [EN][v1_EN_SampleRTMPCluster])
* Usage: How to delivery HTTP FLV Live Streaming?([CN][v2_CN_SampleHttpFlv], [EN][v2_EN_SampleHttpFlv])
* Usage: How to delivery HTTP FLV Live Streaming Cluster?([CN][v2_CN_SampleHttpFlvCluster], [EN][v2_EN_SampleHttpFlvCluster])
* Usage: How to delivery HLS?([CN][v1_CN_SampleHLS], [EN][v1_EN_SampleHLS])
* Usage: How to delivery HLS for other codec?([CN][v1_CN_SampleTranscode2HLS], [EN][v1_EN_SampleTranscode2HLS])
* Usage: How to transode RTMP stream by SRS?([CN][v1_CN_SampleFFMPEG], [EN][v1_EN_SampleFFMPEG])
* Usage: How to forward stream to other server?([CN][v1_CN_SampleForward], [EN][v1_EN_SampleForward])
* Usage: How to deploy low lantency application?([CN][v2_CN_SampleRealtime], [EN][v2_EN_SampleRealtime])
* Usage: How to deploy SRS on ARM?([CN][v1_CN_SampleARM], [EN][v1_EN_SampleARM])
* Usage: How to ingest file/stream/device to SRS?([CN][v1_CN_SampleIngest], [EN][v1_EN_SampleIngest])
* Usage: How to use SRS-HTTP-server to delivery HTTP/HLS stream?([CN][v2_CN_SampleHTTP], [EN][v2_EN_SampleHTTP])
* Usage: How to show the demo of [CN][v1_CN_SampleDemo], [EN][v1_EN_SampleDemo])
* Usage: How to publish h.264 raw stream to [CN][v2_CN_SrsLibrtmp2], [EN][v2_EN_SrsLibrtmp2])
* Usage: Solution using SRS?([CN][v1_CN_Sample], [EN][v1_EN_Sample])
* Usage: Why SRS?([CN][v1_CN_Product], [EN][v1_EN_Product])

## Wiki

SRS 1.0 wiki

Please select your language:
* [SRS 1.0 English][v1_EN_Home]
* [SRS 1.0 Chinese][v1_CN_Home]

SRS 2.0 wiki

Please select your language:
* [SRS 2.0 English][v2_EN_Home]
* [SRS 2.0 Chinese][v2_CN_Home]

## Donation

Donation:<br/>
[http://winlinvip.github.io/srs.release/donation/index.html][donation0] OR <br/>
[http://www.ossrs.net/srs.release/donation/index.html][donation1]

Donations:<br/>
[https://github.com/simple-rtmp-server/srs/blob/develop/DONATIONS.txt][donations]

## System Requirements
Supported operating systems and hardware:
* All Linux , both 32 and 64 bits
* Apple OSX(Darwin), both 32 and 64bits.
* All hardware with x86/x86_64/arm/mips cpu.

## Features

1. Simple, also stable enough.
1. High-performance([CN][v1_CN_Performance], [EN][v1_EN_Performance]): single-thread, async socket, event/st-thread driven.
1. High-concurrency([CN][v1_CN_Performance], [EN][v1_EN_Performance]), 6000+ connections(500kbps), 900Mbps, CPU 90.2%, 41MB
1. Support RTMP Origin Server([CN][v1_CN_DeliveryRTMP],[EN][v1_EN_DeliveryRTMP])
1. Support RTMP Edge Server([CN][v1_CN_Edge], [EN][v1_EN_Edge]) for CDN, push/pull stream from any RTMP server
1. Support single process; no multiple processes.
1. Support Vhost([CN][v1_CN_RtmpUrlVhost], [EN][v1_EN_RtmpUrlVhost]), support \_\_defaultVhost\_\_.
1. Support RTMP([CN][v1_CN_DeliveryRTMP], [EN][v1_EN_DeliveryRTMP]) live streaming; no vod streaming.
1. Support Apple HLS(m3u8)([CN][v1_CN_DeliveryHLS], [EN][v1_EN_DeliveryHLS]) live streaming.
1. Support HLS audio-only([CN][v1_CN_DeliveryHLS2], [EN][v1_EN_DeliveryHLS2]) live streaming.
1. Support Reload([CN][v1_CN_Reload], [EN][v1_EN_Reload]) config to enable changes.
1. Support cache last gop([CN][v1_CN_LowLatency2], [EN][v1_EN_LowLatency2]) for flash player to fast startup.
1. Support listen at multiple ports.
1. Support long time(>4.6hours) publish/play.
1. Support Forward([CN][v1_CN_Forward], [EN][v1_EN_Forward]) in master-slave mode.
1. Support live stream Transcoding([CN][v1_CN_FFMPEG], [EN][v1_EN_FFMPEG]) by ffmpeg.
1. Support ffmpeg([CN][v1_CN_FFMPEG], [EN][v1_EN_FFMPEG]) filters(logo/overlay/crop), x264 params, copy/vn/an.
1. Support audio transcode([CN][v1_CN_FFMPEG], [EN][v1_EN_FFMPEG]) only, speex/mp3 to aac
1. Support http callback api hooks([CN][v1_CN_HTTPCallback], [EN][v1_EN_HTTPCallback])(for authentication and injection).
1. Support bandwidth test([CN][v1_CN_BandwidthTestTool], [EN][v1_EN_BandwidthTestTool]) api and flash client.
1. Player, publisher(encoder), and demo pages(jquery+bootstrap)([CN][v1_CN_SampleDemo],[EN][v1_EN_SampleDemo]). 
1. Demo([CN][v1_CN_SampleDemo], [EN][v1_EN_SampleDemo]) video meeting or chat(SRS+cherrypy+jquery+bootstrap). 
1. Full documents in wiki([CN][v1_CN_Home], [EN][v1_EN_Home]), both Chinese and English. 
1. Support RTMP(play-publish) library: srs-librtmp([CN][v2_CN_SrsLibrtmp], [EN][v2_EN_SrsLibrtmp])
1. Support ARM cpu arch([CN][v1_CN_SrsLinuxArm], [EN][v1_EN_SrsLinuxArm]) with rtmp/ssl/hls/librtmp.
1. Support init.d([CN][v1_CN_LinuxService], [EN][v1_EN_LinuxService]) and packge script, log to file. 
1. Support RTMP ATC([CN][v1_CN_RTMP-ATC], [EN][v1_EN_RTMP-ATC]) for HLS/HDS to support backup(failover)
1. Support HTTP RESTful management api([CN][v1_CN_HTTPApi], [EN][v1_EN_HTTPApi]).
1. Support Ingest([CN][v1_CN_Ingest], [EN][v1_EN_Ingest]) FILE/HTTP/RTMP/RTSP(RTP, SDP) to RTMP using external tools(e.g ffmepg).
1. Support DVR([CN][v1_CN_DVR], [EN][v1_EN_DVR]), record live to flv file for vod.
1. Support tracable log, session based log([CN][v1_CN_SrsLog], [EN][v1_EN_SrsLog]).
1. Support DRM token traverse([CN][v1_CN_DRM2], [EN][v1_EN_DRM2]) for fms origin authenticate.
1. Support system full utest on gtest.
1. Support embeded HTTP server([CN][v2_CN_SampleHTTP], [EN][v2_EN_SampleHTTP]) for hls(live/vod)
1. Support vod stream(http flv/hls vod stream)([CN][v2_CN_FlvVodStream], [EN][v2_EN_FlvVodStream]).
1. Stable [1.0release branch](https://github.com/simple-rtmp-server/srs/tree/1.0release).
1. Support publish h264 raw stream([CN][v2_CN_SrsLibrtmp2], [EN][v2_EN_SrsLibrtmp2]) by srs-librtmp.
1. Support [6k+ clients][bug #194], 3Gbps per process.
1. Suppport [English wiki][v1_EN_Home).
1. Research and simplify st, [bug #182][bug #182].
1. Support compile [srs-librtmp on windows](https://github.com/winlinvip/srs.librtmp), [bug #213][bug #213].
1. Support [10k+ clients][bug #251], 4Gbps per process.
1. Support publish aac adts raw stream([CN][v2_CN_SrsLibrtmp3], [EN][v2_EN_SrsLibrtmp3]) by srs-librtmp.
1. Support 0.1s+ latency, read [bug #257][bug #257].
1. Support allow/deny publish/play for all or specified ip([CN][v2_CN_Security], [EN][v2_EN_Security]).
1. Support custom dvr path and http callback, read [bug #179][bug #179] and [bug #274][bug #274].
1. Support rtmp remux to http flv/mp3/aac/ts live stream, read [bug #293][bug #293]([CN][v2_CN_DeliveryHttpStream], [EN][v2_CN_DeliveryHttpStream]).
1. Support HLS(h.264+mp3) streaming, read [bug #301][bug #301].
1. Rewrite HLS(h.264+aac/mp3) streaming, read [bug #304][bug #304].
1. Support Adobe HDS(f4m)([CN][v1_CN_DeliveryHDS], [EN][v1_EN_DeliveryHDS]) dynamic streaming.
1. [dev] Realease [2.0release branch][branch2].
1. [experiment] Support push MPEG-TS over UDP to SRS, read [bug #250][bug #250].
1. [experiment] Support push RTSP to SRS, read [bug #133][bug #133].
1. [experiment] Support push flv stream over HTTP POST to SRS, read [wiki]([CN][v2_CN_Streamer2], [EN][v2_EN_Streamer2]).
1. [no-plan] Support <500ms latency, FRSC(Fast RTMP-compatible Stream Channel tech).
1. [no-plan] Support RTMP 302 redirect [bug #92][bug #92].
1. [no-plan] Support multiple processes, for both origin and edge
1. [no-plan] Support adobe RTMFP(flash p2p) protocol.
1. [no-plan] Support adobe flash refer/token/swf verification.
1. [no-plan] Support adobe amf3 codec.
1. [no-plan] Support encryption: RTMPE/RTMPS, HLS DRM
1. [no-plan] Support RTMPT, http to tranverse firewalls
1. [no-plan] Support file source, transcoding file to live stream

## Compare

Compare SRS with other media server.

### Stream Delivery

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   RTMP        |   Stable  |   Stable  |   Stable  |   Stable  |   Stable  |
|   HLS         |   Stable  |   Stable  |   X       |   Stable  |   Stable  |
|   HDS         |   Stable  |   X       |   X       |   Stable  |   Stable  |
|   HTTP FLV    |   Stable  |   X       |   X       |   X       |   X       |
|   HLS(aonly)  |   Stable  |   X       |   X       |   Stable  |   Stable  |
|   HTTP Server |   Stable  |   Stable  |   X       |   X       |   Stable  |

### Cluster

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   RTMP Edge   |   Stable  |   X       |   X       |   Stable  |   X       |
|   RTMP Backup |   Stable  |   X       |   X       |   X       |   X       |
|   VHOST       |   Stable  |   X       |   X       |   Stable  |   Stable  |
|   Reload      |   Stable  |   Stable  |   X       |   X       |   X       |
|   Forward     |   Stable  |   X       |   X       |   X       |   X       |
|   ATC         |   Stable  |   X       |   X       |   X       |   X       |

### Stream Service

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   DVR         |   Stable  |   Stable  |   X       |   X       |   Stable  |
|   Transcode   |   Stable  |   X       |   X       |   X       |   Stable  |
|   HTTP API    |   Stable  |   Stable  |   X       |   X       |   Stable  |
|   HTTP hooks  |   Stable  |   X       |   X       |   X       |   X       |
|   GopCache    |   Stable  |   X       |   X       |   Stable  |   X       |
|   Security    |   Stable  |   Stable  |   X       |   X       |   Stable  |
| Token Traverse|   Stable  |   X       |   X       |   Stable  |   X       |

### Performance

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   Concurrency |   7.5k    |   3k      |   2k      |   2k      |   3k      |
|   RTMP Latency|   0.1s    |   3s      |   3s      |   3s      |   3s      |
|   HLS Latency |   10s     |   30s     |   X       |   30s     |   30s     |

### Stream Caster

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   Ingest      |   Stable  |   X       |   X       |   X       |   X       |
|   Push MPEGTS | Experiment|   X       |   X       |   X       |   Stable  |
|   Push RTSP   | Experiment|   X       |   Stable  |   X       |   Stable  |
| Push HTTP FLV | Experiment|   X       |   X       |   X       |   X       |

### Debug System

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   BW check    |   Stable  |   X       |   X       |   X       |   X       |
| Tracable Log  |   Stable  |   X       |   X       |   X       |   X       |

### Docs

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   Demos       |   Stable  |   X       |   X       |   X       |   X       |
|   WIKI(EN+CN) |   Stable  |  EN only  |   X       |   X       |   Stable  |

### Others 

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   ARM/MIPS    |   Stable  |   Stable  |   X       |   X       |   X       |
| Client Library|   Stable  |   X       |   X       |   X       |   X       |

Remark:

1. Concurrency: The concurrency of single process.
1. HLS aonly: The HLS audio only streaming delivery.
1. BW check: The bandwidth check.
1. Security: To allow or deny stream publish or play.

## Releases

* 2015-05-23, [Release v1.0r4](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r4), bug fixed, 1.0.32, 59509 lines.<br/>
* 2015-03-19, [Release v1.0r3](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r3), bug fixed, 1.0.30, 59511 lines.<br/>
* 2015-02-12, [Release v1.0r2](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r2), bug fixed, 1.0.27, 59507 lines.<br/>
* 2015-01-15, [Release v1.0r1](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r1), bug fixed, 1.0.21, 59472 lines.<br/>
* 2014-12-05, [Release v1.0r0](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r0), all bug fixed, 1.0.10, 59391 lines.<br/>
* 2014-10-09, [Release v1.0-beta](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.beta), all bug fixed, 1.0.0, 59316 lines.<br/>
* 2014-08-03, [Release v1.0-mainline7](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline7), config utest, all bug fixed. 57432 lines.<br/>
* 2014-07-13, [Release v1.0-mainline6](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline6), core/kernel/rtmp utest, refine bandwidth(as/js/srslibrtmp library). 50029 lines.<br/>
* 2014-06-27, [Release v1.0-mainline5](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline5), refine perf 3k+ clients, edge token traverse, [srs monitor](http://ossrs.net:1977), 30days online. 41573 lines.<br/>
* 2014-05-28, [Release v1.0-mainline4](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline4), support heartbeat, tracable log, fix mem leak and bugs. 39200 lines.<br/>
* 2014-05-18, [Release v1.0-mainline3](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline3), support mips, fms origin, json(http-api). 37594 lines.<br/>
* 2014-04-28, [Release v1.0-mainline2](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline2), support [dvr](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DVR), android, [edge](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Edge). 35255 lines.<br/>
* 2014-04-07, [Release v1.0-mainline](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline), support [arm](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLinuxArm), [init.d](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_LinuxService), http [server](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPServer)/[api](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPApi), [ingest](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleIngest). 30000 lines.<br/>
* 2013-12-25, [Release v0.9](https://github.com/simple-rtmp-server/srs/releases/tag/0.9), support bandwidth test, player/encoder/chat [demos](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleDemo). 20926 lines.<br/>
* 2013-12-08, [Release v0.8](https://github.com/simple-rtmp-server/srs/releases/tag/0.8), support [http hooks callback](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPCallback), update [st_load](https://github.com/winlinvip/st-load). 19186 lines.<br/>
* 2013-12-03, [Release v0.7](https://github.com/simple-rtmp-server/srs/releases/tag/0.7), support [live stream transcoding](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_FFMPEG). 17605 lines.<br/>
* 2013-11-29, [Release v0.6](https://github.com/simple-rtmp-server/srs/releases/tag/0.6), support [forward](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Forward) stream to origin/edge. 16094 lines.<br/>
* 2013-11-26, [Release v0.5](https://github.com/simple-rtmp-server/srs/releases/tag/0.5), support [HLS(m3u8)](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DeliveryHLS), fragment and window. 14449 lines.<br/>
* 2013-11-10, [Release v0.4](https://github.com/simple-rtmp-server/srs/releases/tag/0.4), support [reload](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Reload) config, pause, longtime publish/play. 12500 lines.<br/>
* 2013-11-04, [Release v0.3](https://github.com/simple-rtmp-server/srs/releases/tag/0.3), support [vhost](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_RtmpUrlVhost), refer, gop cache, listen multiple ports. 11773 lines.<br/>
* 2013-10-25, [Release v0.2](https://github.com/simple-rtmp-server/srs/releases/tag/0.2), support [rtmp](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_RTMPHandshake) flash publish, h264, time jitter correct. 10125 lines.<br/>
* 2013-10-23, [Release v0.1](https://github.com/simple-rtmp-server/srs/releases/tag/0.1), support [rtmp FMLE/FFMPEG publish](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DeliveryRTMP), vp6. 8287 lines.<br/>
* 2013-10-17, Created.<br/>

## History

## SRS 3.0 history

* v3.0, 2015-03-15, fork srs2 and start srs3. 3.0.0

### SRS 2.0 history

* v2.0, 2015-05-24, fix [#404](https://github.com/simple-rtmp-server/srs/issues/404) register handler then start http thread. 2.0.167.
* v2.0, 2015-05-23, refine the thread, protocol, kbps code. 2.0.166
* v2.0, 2015-05-23, fix [#391](https://github.com/simple-rtmp-server/srs/issues/391) copy request for async call.
* v2.0, 2015-05-22, fix [#397](https://github.com/simple-rtmp-server/srs/issues/397) the USER_HZ maybe not 100. 2.0.165
* v2.0, 2015-05-22, for [#400](https://github.com/simple-rtmp-server/srs/issues/400), parse when got entire http header, by feilong. 2.0.164.
* v2.0, 2015-05-19, merge from bravo system, add the rtmfp to bms(commercial srs). 2.0.163.
* v2.0, 2015-05-10, support push flv stream over HTTP POST to SRS.
* v2.0, 2015-04-20, support ingest hls live stream to RTMP.
* v2.0, 2015-04-15, for [#383](https://github.com/simple-rtmp-server/srs/issues/383), support mix_correct algorithm. 2.0.161.
* v2.0, 2015-04-13, for [#381](https://github.com/simple-rtmp-server/srs/issues/381), support reap hls/ts by gop or not. 2.0.160.
* v2.0, 2015-04-10, enhanced on_hls_notify, support HTTP GET when reap ts.
* v2.0, 2015-04-10, refine the hls deviation for floor algorithm.
* v2.0, 2015-04-08, for [#375](https://github.com/simple-rtmp-server/srs/issues/375), fix hls bug, keep cc continous between ts files. 2.0.159.
* v2.0, 2015-04-04, for [#304](https://github.com/simple-rtmp-server/srs/issues/304), rewrite annexb mux for ts, refer to apple sample. 2.0.157.
* v2.0, 2015-04-03, enhanced avc decode, parse the sps get width+height. 2.0.156.
* v2.0, 2015-04-03, for [#372](https://github.com/simple-rtmp-server/srs/issues/372), support transform vhost of edge 2.0.155.
* v2.0, 2015-03-30, for [#366](https://github.com/simple-rtmp-server/srs/issues/366), config hls to disable cleanup of ts. 2.0.154.
* v2.0, 2015-03-31, support server cycle handler. 2.0.153.
* v2.0, 2015-03-31, support on_hls for http hooks. 2.0.152.
* v2.0, 2015-03-31, enhanced hls, support deviation for duration. 2.0.151.
* v2.0, 2015-03-30, for [#351](https://github.com/simple-rtmp-server/srs/issues/351), support config the m3u8/ts path for hls. 2.0.149.
* v2.0, 2015-03-17, for [#155](https://github.com/simple-rtmp-server/srs/issues/155), osx(darwin) support demo with nginx and ffmpeg. 2.0.143.
* v2.0, 2015-03-15, start [2.0release branch](https://github.com/simple-rtmp-server/srs/tree/2.0release), 80773 lines.
* v2.0, 2015-03-14, fix [#324](https://github.com/simple-rtmp-server/srs/issues/324), support hstrs(http stream trigger rtmp source) edge mode. 2.0.140.
* v2.0, 2015-03-14, for [#324](https://github.com/simple-rtmp-server/srs/issues/324), support hstrs(http stream trigger rtmp source) origin mode. 2.0.139.
* v2.0, 2015-03-12, fix [#328](https://github.com/simple-rtmp-server/srs/issues/328), support adobe hds. 2.0.138.
* v2.0, 2015-03-10, fix [#155](https://github.com/simple-rtmp-server/srs/issues/155), support osx(darwin) for mac pro. 2.0.137.
* v2.0, 2015-03-08, fix [#316](https://github.com/simple-rtmp-server/srs/issues/316), http api provides stream/vhost/srs/server bytes, codec and count. 2.0.136.
* v2.0, 2015-03-08, fix [#310](https://github.com/simple-rtmp-server/srs/issues/310), refine aac LC, support aac HE/HEv2. 2.0.134.
* v2.0, 2015-03-06, for [#322](https://github.com/simple-rtmp-server/srs/issues/322), fix http-flv stream bug, support multiple streams. 2.0.133.
* v2.0, 2015-03-06, refine http request parse. 2.0.132.
* v2.0, 2015-03-01, for [#179](https://github.com/simple-rtmp-server/srs/issues/179), revert dvr http api. 2.0.128.
* v2.0, 2015-02-24, for [#304](https://github.com/simple-rtmp-server/srs/issues/304), fix hls bug, write pts/dts error. 2.0.124
* v2.0, 2015-02-19, refine dvr, append file when dvr file exists. 2.0.122.
* v2.0, 2015-02-19, refine pithy print to more easyer to use. 2.0.121.
* v2.0, 2015-02-18, fix [#133](https://github.com/simple-rtmp-server/srs/issues/133), support push rtsp to srs. 2.0.120.
* v2.0, 2015-02-17, the join maybe failed, should use a variable to ensure thread terminated. 2.0.119.
* v2.0, 2015-02-15, for [#304](https://github.com/simple-rtmp-server/srs/issues/304), support config default acodec/vcodec. 2.0.118.
* v2.0, 2015-02-15, for [#304](https://github.com/simple-rtmp-server/srs/issues/304), rewrite hls/ts code, support h.264+mp3 for hls. 2.0.117.
* v2.0, 2015-02-12, for [#304](https://github.com/simple-rtmp-server/srs/issues/304), use stringstream to generate m3u8, add hls_td_ratio. 2.0.116.
* v2.0, 2015-02-11, dev code ZhouGuowen for 2.0.115.
* v2.0, 2015-02-10, for [#311](https://github.com/simple-rtmp-server/srs/issues/311), set pcr_base to dts. 2.0.114.
* v2.0, 2015-02-10, fix [the bug](https://github.com/simple-rtmp-server/srs/commit/87519aaae835199e5adb60c0ae2c1cd24939448c) of ibmf format which decoded in annexb.
* v2.0, 2015-02-10, for [#310](https://github.com/simple-rtmp-server/srs/issues/310), downcast aac SSR to LC. 2.0.113
* v2.0, 2015-02-03, fix [#136](https://github.com/simple-rtmp-server/srs/issues/136), support hls without io(in ram). 2.0.112
* v2.0, 2015-01-31, for [#250](https://github.com/simple-rtmp-server/srs/issues/250), support push MPEGTS over UDP to SRS. 2.0.111
* v2.0, 2015-01-29, build libfdk-aac in ffmpeg. 2.0.108
* v2.0, 2015-01-25, for [#301](https://github.com/simple-rtmp-server/srs/issues/301), hls support h.264+mp3, ok for vlc. 2.0.107
* v2.0, 2015-01-25, for [#301](https://github.com/simple-rtmp-server/srs/issues/301), http ts stream support h.264+mp3. 2.0.106
* v2.0, 2015-01-25, hotfix [#268](https://github.com/simple-rtmp-server/srs/issues/268), refine the pcr start at 0, dts/pts plus delay. 2.0.105
* v2.0, 2015-01-25, hotfix [#151](https://github.com/simple-rtmp-server/srs/issues/151), refine pcr=dts-800ms and use dts/pts directly. 2.0.104
* v2.0, 2015-01-23, hotfix [#151](https://github.com/simple-rtmp-server/srs/issues/151), use absolutely overflow to make jwplayer happy. 2.0.103
* v2.0, 2015-01-22, for [#293](https://github.com/simple-rtmp-server/srs/issues/293), support http live ts stream. 2.0.101.
* v2.0, 2015-01-19, for [#293](https://github.com/simple-rtmp-server/srs/issues/293), support http live flv/aac/mp3 stream with fast cache. 2.0.100.
* v2.0, 2015-01-18, for [#293](https://github.com/simple-rtmp-server/srs/issues/293), support rtmp remux to http flv live stream. 2.0.99.
* v2.0, 2015-01-17, fix [#277](https://github.com/simple-rtmp-server/srs/issues/277), refine http server refer to go http-framework. 2.0.98
* v2.0, 2015-01-17, for [#277](https://github.com/simple-rtmp-server/srs/issues/277), refine http api refer to go http-framework. 2.0.97
* v2.0, 2015-01-17, hotfix [#290](https://github.com/simple-rtmp-server/srs/issues/290), use iformat only for rtmp input. 2.0.95
* v2.0, 2015-01-08, hotfix [#281](https://github.com/simple-rtmp-server/srs/issues/281), fix hls bug ignore type-9 send aud. 2.0.93
* v2.0, 2015-01-03, fix [#274](https://github.com/simple-rtmp-server/srs/issues/274), http-callback support on_dvr when reap a dvr file. 2.0.89
* v2.0, 2015-01-03, hotfix to remove the pageUrl for http callback. 2.0.88
* v2.0, 2015-01-03, fix [#179](https://github.com/simple-rtmp-server/srs/issues/179), dvr support custom filepath by variables. 2.0.87
* v2.0, 2015-01-02, fix [#211](https://github.com/simple-rtmp-server/srs/issues/211), support security allow/deny publish/play all/ip. 2.0.86
* v2.0, 2015-01-02, hotfix [#207](https://github.com/simple-rtmp-server/srs/issues/207), trim the last 0 of log. 2.0.85
* v2.0, 2014-01-02, fix [#158](https://github.com/simple-rtmp-server/srs/issues/158), http-callback check http status code ok(200). 2.0.84
* v2.0, 2015-01-02, hotfix [#216](https://github.com/simple-rtmp-server/srs/issues/216), http-callback post in application/json content-type. 2.0.83
* v2.0, 2014-01-02, fix [#263](https://github.com/simple-rtmp-server/srs/issues/263), srs-librtmp flv read tag should init size. 2.0.82
* v2.0, 2015-01-01, hotfix [#270](https://github.com/simple-rtmp-server/srs/issues/270), memory leak for http client post. 2.0.81
* v2.0, 2014-12-12, fix [#266](https://github.com/simple-rtmp-server/srs/issues/266), aac profile is object id plus one. 2.0.80
* v2.0, 2014-12-29, hotfix [#267](https://github.com/simple-rtmp-server/srs/issues/267), the forward dest ep should use server. 2.0.79
* v2.0, 2014-12-29, hotfix [#268](https://github.com/simple-rtmp-server/srs/issues/268), the hls pcr is negative when startup. 2.0.78
* v2.0, 2014-12-22, hotfix [#264](https://github.com/simple-rtmp-server/srs/issues/264), ignore NALU when sequence header to make HLS happy. 2.0.76
* v2.0, 2014-12-20, hotfix [#264](https://github.com/simple-rtmp-server/srs/issues/264), support disconnect publish connect when hls error. 2.0.75
* v2.0, 2014-12-12, fix [#257](https://github.com/simple-rtmp-server/srs/issues/257), support 0.1s+ latency. 2.0.70
* v2.0, 2014-12-08, update wiki for mr([EN](https://github.com/simple-rtmp-server/srs/wiki/v2_EN_LowLatency#merged-read), [CN](https://github.com/simple-rtmp-server/srs/wiki/v2_CN_LowLatency#merged-read)) and mw([EN](https://github.com/simple-rtmp-server/srs/wiki/v2_EN_LowLatency#merged-write), [CN](https://github.com/simple-rtmp-server/srs/wiki/v2_CN_LowLatency#merged-write)).
* v2.0, 2014-12-07, fix [#251](https://github.com/simple-rtmp-server/srs/issues/251), 10k+ clients, use queue cond wait and fast vector. 2.0.67
* v2.0, 2014-12-05, fix [#251](https://github.com/simple-rtmp-server/srs/issues/251), 9k+ clients, use fast cache for msgs queue. 2.0.57
* v2.0, 2014-12-04, fix [#241](https://github.com/simple-rtmp-server/srs/issues/241), add mw(merged-write) config. 2.0.53
* v2.0, 2014-12-04, for [#241](https://github.com/simple-rtmp-server/srs/issues/241), support mr(merged-read) config and reload. 2.0.52.
* v2.0, 2014-12-04, enable [#241](https://github.com/simple-rtmp-server/srs/issues/241) and [#248](https://github.com/simple-rtmp-server/srs/issues/248), +25% performance, 2.5k publisher. 2.0.50
* v2.0, 2014-12-04, fix [#248](https://github.com/simple-rtmp-server/srs/issues/248), improve about 15% performance for fast buffer. 2.0.49
* v2.0, 2014-12-03, fix [#244](https://github.com/simple-rtmp-server/srs/issues/244), conn thread use cond to wait for recv thread error. 2.0.47.
* v2.0, 2014-12-02, merge [#239](https://github.com/simple-rtmp-server/srs/pull/239), traverse the token before response connect. 2.0.45.
* v2.0, 2014-12-02, srs-librtmp support hijack io apis for st-load. 2.0.42.
* v2.0, 2014-12-01, for [#237](https://github.com/simple-rtmp-server/srs/issues/237), refine syscall for recv, supports 1.5k clients. 2.0.41.
* v2.0, 2014-11-30, add qtcreate project file trunk/src/qt/srs/srs-qt.pro. 2.0.39.
* v2.0, 2014-11-29, fix [#235](https://github.com/simple-rtmp-server/srs/issues/235), refine handshake, replace union with template method. 2.0.38.
* v2.0, 2014-11-28, fix [#215](https://github.com/simple-rtmp-server/srs/issues/215), add srs_rtmp_dump tool. 2.0.37.
* v2.0, 2014-11-25, update PRIMARY, AUTHORS, CONTRIBUTORS rule. 2.0.32.
* v2.0, 2014-11-24, fix [#212](https://github.com/simple-rtmp-server/srs/issues/212), support publish aac adts raw stream. 2.0.31.
* v2.0, 2014-11-22, fix [#217](https://github.com/simple-rtmp-server/srs/issues/217), remove timeout recv, support 7.5k+ 250kbps clients. 2.0.30.
* v2.0, 2014-11-21, srs-librtmp add rtmp prefix for rtmp/utils/human apis. 2.0.29.
* v2.0, 2014-11-21, refine examples of srs-librtmp, add srs_print_rtmp_packet. 2.0.28.
* v2.0, 2014-11-20, fix [#212](https://github.com/simple-rtmp-server/srs/issues/212), support publish audio raw frames. 2.0.27
* v2.0, 2014-11-19, fix [#213](https://github.com/simple-rtmp-server/srs/issues/213), support compile [srs-librtmp on windows](https://github.com/winlinvip/srs.librtmp), [bug #213](https://github.com/simple-rtmp-server/srs/issues/213). 2.0.26
* v2.0, 2014-11-18, all wiki translated to English. 2.0.23.
* v2.0, 2014-11-15, fix [#204](https://github.com/simple-rtmp-server/srs/issues/204), srs-librtmp drop duplicated sps/pps(sequence header). 2.0.22.
* v2.0, 2014-11-15, fix [#203](https://github.com/simple-rtmp-server/srs/issues/203), srs-librtmp drop any video before sps/pps(sequence header). 2.0.21.
* v2.0, 2014-11-15, fix [#202](https://github.com/simple-rtmp-server/srs/issues/202), fix memory leak of h.264 raw packet send in srs-librtmp. 2.0.20.
* v2.0, 2014-11-13, fix [#200](https://github.com/simple-rtmp-server/srs/issues/200), deadloop when read/write 0 and ETIME. 2.0.16.
* v2.0, 2014-11-13, fix [#194](https://github.com/simple-rtmp-server/srs/issues/194), writev multiple msgs, support 6k+ 250kbps clients. 2.0.15.
* v2.0, 2014-11-12, fix [#194](https://github.com/simple-rtmp-server/srs/issues/194), optmized st for timeout recv. pulse to 500ms. 2.0.14.
* v2.0, 2014-11-11, fix [#195](https://github.com/simple-rtmp-server/srs/issues/195), remove the confuse code st_usleep(0). 2.0.13.
* v2.0, 2014-11-08, fix [#191](https://github.com/simple-rtmp-server/srs/issues/191), configure --export-librtmp-project and --export-librtmp-single. 2.0.11.
* v2.0, 2014-11-08, fix [#66](https://github.com/simple-rtmp-server/srs/issues/66), srs-librtmp support write h264 raw packet. 2.0.9.
* v2.0, 2014-10-25, fix [#185](https://github.com/simple-rtmp-server/srs/issues/185), AMF0 support 0x0B the date type codec. 2.0.7.
* v2.0, 2014-10-24, fix [#186](https://github.com/simple-rtmp-server/srs/issues/186), hotfix for bug #186, drop connect args when not object. 2.0.6.
* v2.0, 2014-10-24, rename wiki/xxx to wiki/v1_CN_xxx. 2.0.3.
* v2.0, 2014-10-19, fix [#184](https://github.com/simple-rtmp-server/srs/issues/184), support AnnexB in RTMP body for HLS. 2.0.2
* v2.0, 2014-10-18, remove supports for OSX(darwin). 2.0.1.
* v2.0, 2014-10-16, revert github srs README to English. 2.0.0.

### SRS 1.0 history

* <strong>v1.0, 2015-05-23, [1.0r4 release(1.0.32)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r4) released. 59509 lines.</strong>
* v1.0, 2015-05-22, fix [#397](https://github.com/simple-rtmp-server/srs/issues/397) the USER_HZ maybe not 100. 1.0.32
* v1.0, 2015-03-26, fix hls aac adts bug, in aac mux. 1.0.31.
* <strong>v1.0, 2015-03-19, [1.0r3 release(1.0.30)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r3) released. 59511 lines.</strong>
* v1.0, 2015-03-17, remove the osx for 1.0.30.
* v1.0, 2015-02-17, the join maybe failed, should use a variable to ensure thread terminated. 1.0.28.
* <strong>v1.0, 2015-02-12, [1.0r2 release(1.0.27)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r2) released. 59507 lines.</strong>
* v1.0, 2015-02-11, dev code HuKaiqun for 1.0.27.
* v1.0, 2015-02-10, for [#310](https://github.com/simple-rtmp-server/srs/issues/310), the aac profile must be object plus one. 1.0.26
* v1.0, 2015-01-25, hotfix [#268](https://github.com/simple-rtmp-server/srs/issues/268), refine the pcr start at 0, dts/pts plus delay. 1.0.25
* v1.0, 2015-01-25, hotfix [#151](https://github.com/simple-rtmp-server/srs/issues/151), refine pcr=dts-800ms and use dts/pts directly. 1.0.24
* v1.0, 2015-01-23, hotfix [#151](https://github.com/simple-rtmp-server/srs/issues/151), use absolutely overflow to make jwplayer happy. 1.0.23
* v1.0, 2015-01-17, hotfix [#290](https://github.com/simple-rtmp-server/srs/issues/290), use iformat only for rtmp input. 1.0.22
* <strong>v1.0, 2015-01-15, [1.0r1 release(1.0.21)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0r1) released. 59472 lines.</strong>
* v1.0, 2015-01-08, hotfix [#281](https://github.com/simple-rtmp-server/srs/issues/281), fix hls bug ignore type-9 send aud. 1.0.20
* v1.0, 2015-01-03, hotfix to remove the pageUrl for http callback. 1.0.19
* v1.0, 2015-01-02, hotfix [#207](https://github.com/simple-rtmp-server/srs/issues/207), trim the last 0 of log. 1.0.18
* v1.0, 2015-01-02, hotfix [#216](https://github.com/simple-rtmp-server/srs/issues/216), http-callback post in application/json content-type. 1.0.17
* v1.0, 2015-01-01, hotfix [#270](https://github.com/simple-rtmp-server/srs/issues/270), memory leak for http client post. 1.0.16
* v1.0, 2014-12-29, hotfix [#267](https://github.com/simple-rtmp-server/srs/issues/267), the forward dest ep should use server. 1.0.15
* v1.0, 2014-12-29, hotfix [#268](https://github.com/simple-rtmp-server/srs/issues/268), the hls pcr is negative when startup. 1.0.14
* v1.0, 2014-12-22, hotfix [#264](https://github.com/simple-rtmp-server/srs/issues/264), ignore NALU when sequence header to make HLS happy. 1.0.12
* v1.0, 2014-12-20, hotfix [#264](https://github.com/simple-rtmp-server/srs/issues/264), support disconnect publish connect when hls error. 1.0.11
* <strong>v1.0, 2014-12-05, [1.0 release(1.0.10)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0) released. 59391 lines.</strong>
* <strong>v1.0, 2014-10-09, [1.0 beta(1.0.0)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.beta) released. 59316 lines.</strong>
* v1.0, 2014-10-08, fix [#151](https://github.com/simple-rtmp-server/srs/issues/151), always reap ts whatever audio or video packet. 0.9.223.
* v1.0, 2014-10-08, fix [#162](https://github.com/simple-rtmp-server/srs/issues/162), failed if no epoll. 0.9.222.
* v1.0, 2014-09-30, fix [#180](https://github.com/simple-rtmp-server/srs/issues/180), crash for multiple edge publishing the same stream. 0.9.220.
* v1.0, 2014-09-26, fix hls bug, refine config and log, according to clion of jetbrains. 0.9.216. 
* v1.0, 2014-09-25, fix [#177](https://github.com/simple-rtmp-server/srs/issues/177), dvr segment add config dvr_wait_keyframe. 0.9.213.
* v1.0, 2014-08-28, fix [#167](https://github.com/simple-rtmp-server/srs/issues/167), add openssl includes to utest. 0.9.209.
* v1.0, 2014-08-27, max connections is 32756, for st use mmap default. 0.9.209
* v1.0, 2014-08-24, fix [#150](https://github.com/simple-rtmp-server/srs/issues/150), forward should forward the sequence header when retry. 0.9.208.
* v1.0, 2014-08-22, for [#165](https://github.com/simple-rtmp-server/srs/issues/165), refine dh wrapper, ensure public key is 128bytes. 0.9.206.
* v1.0, 2014-08-19, for [#160](https://github.com/simple-rtmp-server/srs/issues/160), support forward/edge to flussonic, disable debug_srs_upnode to make flussonic happy. 0.9.201.
* v1.0, 2014-08-17, for [#155](https://github.com/simple-rtmp-server/srs/issues/155), refine for osx, with ssl/http, disable statistics. 0.9.198.
* v1.0, 2014-08-06, fix [#148](https://github.com/simple-rtmp-server/srs/issues/148), simplify the RTMP handshake key generation. 0.9.191.
* v1.0, 2014-08-06, fix [#147](https://github.com/simple-rtmp-server/srs/issues/147), support identify the srs edge. 0.9.190.
* <strong>v1.0, 2014-08-03, [1.0 mainline7(0.9.189)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline7) released. 57432 lines.</strong>
* v1.0, 2014-08-03, fix [#79](https://github.com/simple-rtmp-server/srs/issues/79), fix the reload remove edge assert bug. 0.9.189.
* v1.0, 2014-08-03, fix [#57](https://github.com/simple-rtmp-server/srs/issues/57), use lock(acquire/release publish) to avoid duplicated publishing. 0.9.188.
* v1.0, 2014-08-03, fix [#85](https://github.com/simple-rtmp-server/srs/issues/85), fix the segment-dvr sequence header missing. 0.9.187.
* v1.0, 2014-08-03, fix [#145](https://github.com/simple-rtmp-server/srs/issues/145), refine ffmpeg log, check abitrate for libaacplus. 0.9.186.
* v1.0, 2014-08-03, fix [#143](https://github.com/simple-rtmp-server/srs/issues/143), fix retrieve sys stat bug for all linux. 0.9.185.
* v1.0, 2014-08-02, fix [#138](https://github.com/simple-rtmp-server/srs/issues/138), fix http hooks bug, regression bug. 0.9.184.
* v1.0, 2014-08-02, fix [#142](https://github.com/simple-rtmp-server/srs/issues/142), fix tcp stat slow bug, use /proc/net/sockstat instead, refer to 'ss -s'. 0.9.183.
* v1.0, 2014-07-31, fix [#141](https://github.com/simple-rtmp-server/srs/issues/141), support tun0(vpn network device) ip retrieve. 0.9.179.
* v1.0, 2014-07-27, support partially build on OSX(Darwin). 0.9.177
* v1.0, 2014-07-27, api connections add udp, add disk iops. 0.9.176
* v1.0, 2014-07-26, complete config utest. 0.9.173
* v1.0, 2014-07-26, fix [#124](https://github.com/simple-rtmp-server/srs/issues/124), gop cache support disable video in publishing. 0.9.171.
* v1.0, 2014-07-23, fix [#121](https://github.com/simple-rtmp-server/srs/issues/121), srs_info detail log compile failed. 0.9.168.
* v1.0, 2014-07-19, fix [#119](https://github.com/simple-rtmp-server/srs/issues/119), use iformat and oformat for ffmpeg transcode. 0.9.163.
* <strong>v1.0, 2014-07-13, [1.0 mainline6(0.9.160)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline6) released. 50029 lines.</strong>
* v1.0, 2014-07-13, refine the bandwidth check/test, add as/js library, use srs-librtmp for linux tool. 0.9.159
* v1.0, 2014-07-12, complete rtmp stack utest. 0.9.156
* v1.0, 2014-07-06, fix [#81](https://github.com/simple-rtmp-server/srs/issues/81), fix HLS codec info, IOS ok. 0.9.153.
* v1.0, 2014-07-06, fix [#103](https://github.com/simple-rtmp-server/srs/issues/103), support all aac sample rate. 0.9.150.
* v1.0, 2014-07-05, complete kernel utest. 0.9.149
* v1.0, 2014-06-30, fix [#111](https://github.com/simple-rtmp-server/srs/issues/111), always use 31bits timestamp. 0.9.143.
* v1.0, 2014-06-28, response the call message with null. 0.9.137
* v1.0, 2014-06-28, fix [#110](https://github.com/simple-rtmp-server/srs/issues/110), thread start segment fault, thread cycle stop destroy thread. 0.9.136
* v1.0, 2014-06-27, fix [#109](https://github.com/simple-rtmp-server/srs/issues/109), fix the system jump time, adjust system startup time. 0.9.135
* <strong>v1.0, 2014-06-27, [1.0 mainline5(0.9.134)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline5) released. 41573 lines.</strong>
* v1.0, 2014-06-27, SRS online 30days with RTMP/HLS.
* v1.0, 2014-06-25, fix [#108](https://github.com/simple-rtmp-server/srs/issues/108), support config time jitter for encoder non-monotonical stream. 0.9.133
* v1.0, 2014-06-23, support report summaries in heartbeat. 0.9.132
* v1.0, 2014-06-22, performance refine, support [3k+](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Performance#%E6%80%A7%E8%83%BD%E4%BE%8B%E8%A1%8C%E6%8A%A5%E5%91%8A4k) connections(270kbps). 0.9.130
* v1.0, 2014-06-21, support edge [token traverse](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DRM#tokentraverse), fix [#104](https://github.com/simple-rtmp-server/srs/issues/104). 0.9.129
* v1.0, 2014-06-19, add connections count to api summaries. 0.9.127
* v1.0, 2014-06-19, add srs bytes and kbps to api summaries. 0.9.126
* v1.0, 2014-06-18, add network bytes to api summaries. 0.9.125
* v1.0, 2014-06-14, fix [#98](https://github.com/simple-rtmp-server/srs/issues/98), workaround for librtmp ping(fmt=1,cid=2 fresh stream). 0.9.124
* v1.0, 2014-05-29, support flv inject and flv http streaming with start=bytes. 0.9.122
* <strong>v1.0, 2014-05-28, [1.0 mainline4(0.9.120)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline4) released. 39200 lines.</strong>
* v1.0, 2014-05-27, fix [#87](https://github.com/simple-rtmp-server/srs/issues/87), add source id for full trackable log. 0.9.120
* v1.0, 2014-05-27, fix [#84](https://github.com/simple-rtmp-server/srs/issues/84), unpublish when edge disconnect. 0.9.119
* v1.0, 2014-05-27, fix [#89](https://github.com/simple-rtmp-server/srs/issues/89), config to /dev/null to disable ffmpeg log. 0.9.117
* v1.0, 2014-05-25, fix [#76](https://github.com/simple-rtmp-server/srs/issues/76), allow edge vhost to add or remove. 0.9.114
* v1.0, 2014-05-24, Johnny contribute [ossrs.net](http://ossrs.net). karthikeyan start to translate wiki to English.
* v1.0, 2014-05-22, fix [#78](https://github.com/simple-rtmp-server/srs/issues/78), st joinable thread must be stop by other threads, 0.9.113
* v1.0, 2014-05-22, support amf0 StrictArray(0x0a). 0.9.111.
* v1.0, 2014-05-22, support flv parser, add amf0 to librtmp. 0.9.110
* v1.0, 2014-05-22, fix [#74](https://github.com/simple-rtmp-server/srs/issues/74), add tcUrl for http callback on_connect, 0.9.109
* v1.0, 2014-05-19, support http heartbeat, 0.9.107
* <strong>v1.0, 2014-05-18, [1.0 mainline3(0.9.105)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline3) released. 37594 lines.</strong>
* v1.0, 2014-05-18, support http api json, to PUT/POST. 0.9.105
* v1.0, 2014-05-17, fix [#72](https://github.com/simple-rtmp-server/srs/issues/72), also need stream_id for send_and_free_message. 0.9.101
* v1.0, 2014-05-17, rename struct to class. 0.9.100
* v1.0, 2014-05-14, fix [#67](https://github.com/simple-rtmp-server/srs/issues/67) pithy print, stage must has a age. 0.9.98
* v1.0, 2014-05-13, fix mem leak for delete[] SharedPtrMessage array. 0.9.95
* v1.0, 2014-05-12, refine the kbps calc module. 0.9.93
* v1.0, 2014-05-12, fix bug [#64](https://github.com/simple-rtmp-server/srs/issues/64): install_dir=DESTDIR+PREFIX
* v1.0, 2014-05-08, fix [#36](https://github.com/simple-rtmp-server/srs/issues/36): never directly use \*(int32_t\*) for arm.
* v1.0, 2014-05-08, fix [#60](https://github.com/simple-rtmp-server/srs/issues/60): support aggregate message
* v1.0, 2014-05-08, fix [#59](https://github.com/simple-rtmp-server/srs/issues/59), edge support FMS origin server. 0.9.92
* v1.0, 2014-05-06, fix [#50](https://github.com/simple-rtmp-server/srs/issues/50), ubuntu14 build error.
* v1.0, 2014-05-04, support mips linux.
* v1.0, 2014-04-30, fix bug [#34](https://github.com/simple-rtmp-server/srs/issues/34): convert signal to io thread. 0.9.85
* v1.0, 2014-04-29, refine RTMP protocol completed, to 0.9.81
* <strong>v1.0, 2014-04-28, [1.0 mainline2(0.9.79)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline2) released. 35255 lines.</strong>
* v1.0, 2014-04-28, support full edge RTMP server. 0.9.79
* v1.0, 2014-04-27, support basic edge(play/publish) RTMP server. 0.9.78
* v1.0, 2014-04-25, add donation page. 0.9.76
* v1.0, 2014-04-21, support android app to start srs for internal edge. 0.9.72
* v1.0, 2014-04-19, support tool over srs-librtmp to ingest flv/rtmp. 0.9.71
* v1.0, 2014-04-17, support dvr(record live to flv file for vod). 0.9.69
* v1.0, 2014-04-11, add speex1.2 to transcode flash encoder stream. 0.9.58
* v1.0, 2014-04-10, support reload ingesters(add/remov/update). 0.9.57
* <strong>v1.0, 2014-04-07, [1.0 mainline(0.9.55)](https://github.com/simple-rtmp-server/srs/releases/tag/1.0.mainline) released. 30000 lines.</strong>
* v1.0, 2014-04-07, support [ingest](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleIngest) file/stream/device.
* v1.0, 2014-04-05, support [http api](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPApi) and [http server](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPServer).
* v1.0, 2014-04-03, implements http framework and api/v1/version.
* v1.0, 2014-03-30, fix bug for st detecting epoll failed, force st to use epoll.
* v1.0, 2014-03-29, add wiki [Performance for RaspberryPi](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_RaspberryPi).
* v1.0, 2014-03-29, add release binary package for raspberry-pi. 
* v1.0, 2014-03-26, support RTMP ATC for HLS/HDS to support backup(failover).
* v1.0, 2014-03-23, support daemon, default start in daemon.
* v1.0, 2014-03-22, support make install/install-api and uninstall.
* v1.0, 2014-03-22, add ./etc/init.d/srs, refine to support make clean then make.
* v1.0, 2014-03-21, write pid to ./objs/srs.pid.
* v1.0, 2014-03-20, refine hls code, support pure audio HLS.
* v1.0, 2014-03-19, add vn/an for FFMPEG to drop video/audio for radio stream.
* v1.0, 2014-03-19, refine handshake, client support complex handshake, add utest.
* v1.0, 2014-03-16, fix bug on arm of st, the sp change from 20 to 8, for respberry-pi, @see [commit](https://github.com/simple-rtmp-server/srs/commit/5a4373d4835758188b9a1f03005cea0b6ddc62aa)
* v1.0, 2014-03-16, support ARM([debian armhf, v7cpu](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLinuxArm)) with rtmp/ssl/hls/librtmp.
* v1.0, 2014-03-12, finish utest for amf0 codec.
* v1.0, 2014-03-06, add gperftools for mem leak detect, mem/cpu profile.
* v1.0, 2014-03-04, add gest framework for utest, build success.
* v1.0, 2014-03-02, add wiki [srs-librtmp](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLibrtmp), [SRS for arm](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLinuxArm), [product](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Product)
* v1.0, 2014-03-02, srs-librtmp, client publish/play library like librtmp.
* v1.0, 2014-03-01, modularity, extract core/kernel/rtmp/app/main module.
* v1.0, 2014-02-28, support arm build(SRS/ST), add ssl to 3rdparty package.
* v1.0, 2014-02-28, add wiki [BuildArm](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Build), [FFMPEG](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_FFMPEG), [Reload](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Reload)
* v1.0, 2014-02-27, add wiki [LowLatency](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_LowLatency), [HTTPCallback](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPCallback), [ServerSideScript](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_ServerSideScript), [IDE](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_IDE)
* v1.0, 2014-01-19, add wiki [DeliveryHLS](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DeliveryHLS)
* v1.0, 2014-01-12, add wiki [HowToAskQuestion](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HowToAskQuestion), [RtmpUrlVhost](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_RtmpUrlVhost)
* v1.0, 2014-01-11, fix jw/flower player pause bug, which send closeStream actually.
* v1.0, 2014-01-05, add wiki [Build](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Build), [Performance](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Performance), [Forward](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Forward)
* v1.0, 2014-01-01, change listen(512), chunk-size(60000), to improve performance.
* v1.0, 2013-12-27, merge from wenjie, the bandwidth test feature.
* <strong>v0.9, 2013-12-25, [v0.9](https://github.com/simple-rtmp-server/srs/releases/tag/0.9) released. 20926 lines.</strong>
* v0.9, 2013-12-25, fix the bitrate bug(in Bps), use enhanced microphone.
* v0.9, 2013-12-22, demo video meeting or chat(SRS+cherrypy+jquery+bootstrap).
* v0.9, 2013-12-22, merge from wenjie, support banwidth test.
* v0.9, 2013-12-22, merge from wenjie: support set chunk size at vhost level
* v0.9, 2013-12-21, add [players](http://demo.srs.com/players) for play and publish.
* v0.9, 2013-12-15, ensure the HLS(ts) is continous when republish stream.
* v0.9, 2013-12-15, fix the hls reload bug, feed it the sequence header.
* v0.9, 2013-12-15, refine protocol, use int64_t timestamp for ts and jitter.
* v0.9, 2013-12-15, support set the live queue length(in seconds), drop when full.
* v0.9, 2013-12-15, fix the forwarder reconnect bug, feed it the sequence header.
* v0.9, 2013-12-15, support reload the hls/forwarder/transcoder.
* v0.9, 2013-12-14, refine the thread model for the retry threads.
* v0.9, 2013-12-10, auto install depends tools/libs on centos/ubuntu.
* <strong>v0.8, 2013-12-08, [v0.8](https://github.com/simple-rtmp-server/srs/releases/tag/0.8) released. 19186 lines.</strong>
* v0.8, 2013-12-08, support [http hooks](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPCallback): on_connect/close/publish/unpublish/play/stop.
* v0.8, 2013-12-08, support multiple http hooks for a event.
* v0.8, 2013-12-07, support http callback hooks, on_connect.
* v0.8, 2013-12-07, support network based cli and json result, add CherryPy 3.2.4.
* v0.8, 2013-12-07, update http/hls/rtmp load test tool [st_load](https://github.com/winlinvip/st-load), use SRS rtmp sdk.
* v0.8, 2013-12-06, support max_connections, drop if exceed.
* v0.8, 2013-12-05, support log_dir, write ffmpeg log to file.
* v0.8, 2013-12-05, fix the forward/hls/encoder bug.
* <strong>v0.7, 2013-12-03, [v0.7](https://github.com/simple-rtmp-server/srs/releases/tag/0.7) released. 17605 lines.</strong>
* v0.7, 2013-12-01, support dead-loop detect for forwarder and transcoder.
* v0.7, 2013-12-01, support all ffmpeg filters and params.
* v0.7, 2013-11-30, support live stream transcoder by ffmpeg.
* v0.7, 2013-11-30, support --with/without -ffmpeg, build ffmpeg-2.1.
* v0.7, 2013-11-30, add ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
* <strong>v0.6, 2013-11-29, [v0.6](https://github.com/simple-rtmp-server/srs/releases/tag/0.6) released. 16094 lines.</strong>
* v0.6, 2013-11-29, add performance summary, 1800 clients, 900Mbps, CPU 90.2%, 41MB.
* v0.6, 2013-11-29, support forward stream to other edge server.
* v0.6, 2013-11-29, support forward stream to other origin server.
* v0.6, 2013-11-28, fix memory leak bug, aac decode bug.
* v0.6, 2013-11-27, support --with or --without -hls and -ssl options.
* v0.6, 2013-11-27, support AAC 44100HZ sample rate for iphone, adjust the timestamp.
* <strong>v0.5, 2013-11-26, [v0.5](https://github.com/simple-rtmp-server/srs/releases/tag/0.5) released. 14449 lines.</strong>
* v0.5, 2013-11-24, support HLS(m3u8), fragment and window.
* v0.5, 2013-11-24, support record to ts file for HLS.
* v0.5, 2013-11-21, add ts_info tool to demux ts file.
* v0.5, 2013-11-16, add rtmp players(OSMF/jwplayer5/jwplayer6).
* <strong>v0.4, 2013-11-10, [v0.4](https://github.com/simple-rtmp-server/srs/releases/tag/0.4) released. 12500 lines.</strong>
* v0.4, 2013-11-10, support config and reload the pithy print.
* v0.4, 2013-11-09, support reload config(vhost and its detail).
* v0.4, 2013-11-09, support reload config(listen and chunk_size) by SIGHUP(1).
* v0.4, 2013-11-09, support longtime(>4.6hours) publish/play.
* v0.4, 2013-11-09, support config the chunk_size.
* v0.4, 2013-11-09, support pause for live stream.
* <strong>v0.3, 2013-11-04, [v0.3](https://github.com/simple-rtmp-server/srs/releases/tag/0.3) released. 11773 lines.</strong>
* v0.3, 2013-11-04, support refer/play-refer/publish-refer.
* v0.3, 2013-11-04, support vhosts specified config.
* v0.3, 2013-11-02, support listen multiple ports.
* v0.3, 2013-11-02, support config file in nginx-conf style.
* v0.3, 2013-10-29, support pithy print log message specified by stage.
* v0.3, 2013-10-28, support librtmp without extended-timestamp in 0xCX chunk packet.
* v0.3, 2013-10-27, support cache last gop for client fast startup.
* <strong>v0.2, 2013-10-25, [v0.2](https://github.com/simple-rtmp-server/srs/releases/tag/0.2) released. 10125 lines.</strong>
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake.
* v0.2, 2013-10-24, support time jitter detect and correct algorithm
* v0.2, 2013-10-24, support decode codec type to cache the h264/avc sequence header.
* <strong>v0.1, 2013-10-23, [v0.1](https://github.com/simple-rtmp-server/srs/releases/tag/0.1) released. 8287 lines.</strong>
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send\_message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv\_message).

## Performance

Performance benchmark history, on virtual box.

* See also: [Performance for x86/x64 Test Guide](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Performance)
* See also: [Performance for RaspberryPi](https://github.com/simple-rtmp-server/srs/wiki/v1_CN_RaspberryPi)

### Play benchmark

The play benchmark by [st-load](https://github.com/winlinvip/st-load):


|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2013-11-28  |   0.5.0   |   1.8k(1800)  |   players     |   90%     |   41M     |   -           |
|   2014-07-12  |   0.9.156 |   1.8k(1800)  |   players     |   68%     |   38MB    |   -           |
|   2014-07-12  |   0.9.156 |   2.7k(2700)  |   players     |   89%     |   61MB    |   [code][p6]  |
|   2014-11-11  |   1.0.5   |   2.7k(2700)  |   players     |   85%     |   66MB    |   -           |
|   2014-11-11  |   2.0.12  |   2.7k(2700)  |   players     |   85%     |   66MB    |   -           |
|   2014-11-12  |   2.0.14  |   2.7k(2700)  |   players     |   69%     |   59MB    |   -           |
|   2014-11-12  |   2.0.14  |   3.5k(3500)  |   players     |   95%     |   78MB    |   [code][p7]  |
|   2014-11-13  |   2.0.15  |   6.0k(6000)  |   players     |   82%     |   203MB   |   [code][p8]  |
|   2014-11-22  |   2.0.30  |   7.5k(7500)  |   players     |   87%     |   320MB   |   [code][p9]  |
|   2014-12-05  |   2.0.55  |   8.0k(8000)  |   players     |   89%     |   360MB   |   [code][p10] |
|   2014-12-05  |   2.0.57  |   9.0k(9000)  |   players     |   90%     |   468MB   |   [code][p11] |
|   2014-12-07  |   2.0.67  |   10k(10000)  |   players     |   95%     |   656MB   |   [code][p12] |

### Publish benchmark

The publish benchmark by [st-load](https://github.com/winlinvip/st-load):

|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2014-12-03  |   1.0.10  |   1.2k(1200)  |   publishers  |   96%     |   43MB    |   -           |
|   2014-12-03  |   2.0.12  |   1.2k(1200)  |   publishers  |   96%     |   43MB    |   -           |
|   2014-12-03  |   2.0.47  |   1.2k(1200)  |   publishers  |   84%     |   76MB    |   [code][p1]  |
|   2014-12-03  |   2.0.47  |   1.4k(1400)  |   publishers  |   95%     |   140MB   |   -           |
|   2014-12-03  |   2.0.48  |   1.4k(1400   |   publishers  |   95%     |   140MB   |   [code][p2]  |
|   2014-12-04  |   2.0.49  |   1.4k(1400)  |   publishers  |   68%     |   144MB   |   -           |
|   2014-12-04  |   2.0.49  |   2.5k(2500)  |   publishers  |   95%     |   404MB   |   [code][p3]  |
|   2014-12-04  |   2.0.51  |   2.5k(2500)  |   publishers  |   91%     |   259MB   |   [code][p4]  |
|   2014-12-04  |   2.0.52  |   4.0k(4000)  |   publishers  |   80%     |   331MB   |   [code][p5]  |

### Latency benchmark

The latency between encoder and player with realtime config(
[CN](https://github.com/simple-rtmp-server/srs/wiki/v2_CN_LowLatency),
[EN](https://github.com/simple-rtmp-server/srs/wiki/v2_EN_LowLatency)
):


|   Update      |    SRS    |    VP6    |  H.264    |  VP6+MP3  | H.264+MP3 |
| ------------- | --------- | --------- | --------- | --------- | --------  |
|   2014-12-03  |   1.0.10  |   0.4s    |   0.4s    |   0.9s    |   1.2s    |
|   2014-12-12  |   2.0.70  |[0.1s][p13]|[0.4s][p14]|   1.0s    |   0.9s    |
|   2014-12-16  |   2.0.72  |   0.1s    |   0.4s    |[0.8s][p15]|[0.6s][p16]|

We use FMLE as encoder for benchmark. The latency of server is 0.1s+, 
and the bottleneck is the encoder. For more information, read 
[bug #257](https://github.com/simple-rtmp-server/srs/issues/257#issuecomment-66864413).

## Architecture

SRS always use the most simple architecture to support complex transaction.
* System arch: the system structure and arch.
* Modularity arch: the main modularity of SRS.
* Stream arch: the stream dispatch arch of SRS.

### System Architecture

<pre>
+------------------------------------------------------+
|             SRS(Simple RTMP Server)                  |
+---------------+---------------+-----------+----------+
|   API/hook    |   Transcoder  |  HLS/HDS  | RTMP/FLV |
|  http-parser  |  FFMPEG/x264  |  NGINX/ts | protocol |
+---------------+---------------+-----------+----------+
|              Network(state-threads)                  |
+------------------------------------------------------+
|    All Linux/Unix(RHEL,CentOS,Ubuntu,Fedora...)      |
+------------------------------------------------------+
</pre>

### Modularity Architecture

<pre>
+------------------------------------------------------+
|             Main(srs/ingest-hls/librtmp)             |
+------------------------------------------------------+
|           App(Server/Client application)             |
+------------------------------------------------------+
|         RTMP/HTTP/RawStream(Protocol stack)          |
+------------------------------------------------------+
|      Kernel(depends on Core, provides error/log)     |
+------------------------------------------------------+
|         Core(depends only on system apis)            |
+------------------------------------------------------+
</pre>

### Stream Architecture

<pre>
                   +---------+              +----------+
                   + Publish +              +  Deliver |
                   +---|-----+              +----|-----+
+----------------------+-------------------------+----------------+
|     Input            | SRS(Simple RTMP Server) |     Output     |
+----------------------+-------------------------+----------------+
|    Encoder(1)        |   +-> RTMP/HDS  --------+-> Flash player |
|  (FMLE,FFMPEG, -rtmp-+->-+-> HLS/HTTP ---------+-> M3u8 player  |
|  Flash,XSPLIT,       |   +-> FLV/MP3/Aac/Ts ---+-> HTTP player  |
|  ......)             |   +-> Fowarder ---------+-> RTMP server  |
|                      |   +-> Transcoder -------+-> RTMP server  |
|                      |   +-> DVR --------------+-> Flv file     |
|                      |   +-> BandwidthTest ----+-> flash        |
+----------------------+                         |                |
|  MediaSource(2)      |                         |                |
|  (RTSP,FILE,         |                         |                |
|   HTTP,HLS,   --pull-+->-- Ingester ----(rtmp)-+-> SRS          |
|   Device,            |                         |                |
|   ......)            |                         |                |
+----------------------+                         |                |
|  MediaSource(2)      |                         |                |
|  (RTSP,FILE,         |                         |                |
|   HTTP,HLS,   --push-+->-- Streamer ----(rtmp)-+-> SRS          |
|   Device,            |                         |                |
|   ......)            |                         |                |
+----------------------+-------------------------+----------------+

Remark:
(1) Encoder: encoder must push RTMP stream to SRS server.
(2) MediaSource: any media source, which can be ingest by ffmpeg.
(3) Ingester: SRS will fork a process to run ffmpeg(or your application) 
to ingest any input to rtmp, push to SRS. Read <a href="https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Ingest">Ingest</a>.
(4) Streamer: SRS will listen for some protocol and accept stream push 
over some protocol and remux to rtmp to SRS. Read <a href="https://github.com/simple-rtmp-server/srs/wiki/v2_CN_Streamer">Streamer</a>.
</pre>

Beijing, 2013.10<br/>
Winlin

[p1]: https://github.com/simple-rtmp-server/srs/commit/787ab674e38734ea8e0678101614fdcd84645dc8
[p2]: https://github.com/simple-rtmp-server/srs/commit/f35ec2155b1408d528a9f37da7904c9625186bcf
[p3]: https://github.com/simple-rtmp-server/srs/commit/29324fab469e0f7cef9ad04ffdbce832ac7dd9ff
[p4]: https://github.com/simple-rtmp-server/srs/commit/f57801eb46c16755b173984b915a4166922df6a6
[p5]: https://github.com/simple-rtmp-server/srs/commit/5589b13d2e216b91f97afb78ee0c011b2fccf7da
[p6]: https://github.com/simple-rtmp-server/srs/commit/1ae3e6c64cc5cee90e6050c26968ebc3c18281be
[p7]: https://github.com/simple-rtmp-server/srs/commit/8acd143a7a152885b815999162660fd4e7a3f247
[p8]: https://github.com/simple-rtmp-server/srs/commit/cc6aca9ad55342a06440ce7f3b38453776b2b2d1
[p9]: https://github.com/simple-rtmp-server/srs/commit/58136ec178e3d47db6c90a59875d7e40946936e5
[p10]: https://github.com/simple-rtmp-server/srs/commit/58136ec178e3d47db6c90a59875d7e40946936e5
[p11]: https://github.com/simple-rtmp-server/srs/commit/9ee138746f83adc26f0e236ec017f4d68a300004
[p12]: https://github.com/simple-rtmp-server/srs/commit/1311b6fe6576fd7b9c6d299b0f8f2e8d202f4bf8
[p13]: https://github.com/simple-rtmp-server/srs/commit/10297fab519811845b549a8af40a6bcbd23411e8
[p14]: https://github.com/simple-rtmp-server/srs/commit/10297fab519811845b549a8af40a6bcbd23411e8
[p15]: https://github.com/simple-rtmp-server/srs/commit/0d6b91039d408328caab31a1077d56a809b6bebc
[p16]: https://github.com/simple-rtmp-server/srs/commit/0d6b91039d408328caab31a1077d56a809b6bebc

[authors]: https://github.com/simple-rtmp-server/srs/blob/develop/AUTHORS.txt
[bigthanks]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Product#bigthanks
[st]: https://github.com/winlinvip/state-threads
[st2]: http://sourceforge.net/projects/state-threads/
[state-threads]: http://sourceforge.net/projects/state-threads/
[nginx-rtmp]: https://github.com/arut/nginx-rtmp-module
[http-parser]: https://github.com/joyent/http-parser
[nginx]: http://nginx.org/
[FFMPEG]: http://ffmpeg.org/
[libx264]: http://www.videolan.org/
[srs]: https://github.com/simple-rtmp-server/srs
[csdn]: https://code.csdn.net/winlinvip/srs-csdn
[oschina]: http://git.oschina.net/winlinvip/srs.oschina
[gitlab]: https://gitlab.com/winlinvip/srs-gitlab
[v1_CN_Git]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Git
[v1_EN_Git]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Git
[v1_CN_SampleRTMP]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleRTMP
[v1_EN_SampleRTMP]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleRTMP
[v1_CN_SampleRTMPCluster]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleRTMPCluster
[v1_EN_SampleRTMPCluster]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleRTMPCluster
[v1_CN_SampleHLS]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleHLS
[v1_EN_SampleHLS]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleHLS
[v1_CN_SampleTranscode2HLS]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleTranscode2HLS
[v1_EN_SampleTranscode2HLS]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleTranscode2HLS
[v1_CN_SampleFFMPEG]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleFFMPEG
[v1_EN_SampleFFMPEG]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleFFMPEG
[v1_CN_SampleForward]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleForward
[v1_EN_SampleForward]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleForward
[v2_CN_SampleRealtime]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SampleRealtime
[v2_EN_SampleRealtime]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SampleRealtime
[v1_CN_SampleARM]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleARM
[v1_EN_SampleARM]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleARM
[v1_CN_SampleIngest]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleIngest
[v1_EN_SampleIngest]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleIngest
[v1_CN_SampleHTTP]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleHTTP
[v1_EN_SampleHTTP]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleHTTP
[v1_CN_SampleDemo]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleDemo
[v1_EN_SampleDemo]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleDemo
[v2_CN_SrsLibrtmp2]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SrsLibrtmp#publish-h264-raw-data
[v2_EN_SrsLibrtmp2]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SrsLibrtmp#publish-h264-raw-data
[v1_CN_Sample]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Sample
[v1_EN_Sample]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Sample
[v1_CN_Product]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Product
[v1_EN_Product]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Product
[v1_CN_Home]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Home
[v1_EN_Home]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Home
[v2_CN_Home]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_Home
[v2_EN_Home]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_Home
[donation0]: http://winlinvip.github.io/srs.release/donation/index.html
[donation1]: http://www.ossrs.net/srs.release/donation/index.html
[donations]: https://github.com/simple-rtmp-server/srs/blob/develop/DONATIONS.txt

[v1_CN_Performance]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Performance
[v1_EN_Performance]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Performance
[v1_CN_DeliveryRTMP]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DeliveryRTMP
[v1_EN_DeliveryRTMP]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_DeliveryRTMP
[v1_CN_Edge]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Edge
[v1_EN_Edge]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Edge
[v1_CN_RtmpUrlVhost]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_RtmpUrlVhost
[v1_EN_RtmpUrlVhost]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_RtmpUrlVhost
[v1_CN_DeliveryHLS]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DeliveryHLS
[v1_EN_DeliveryHLS]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_DeliveryHLS
[v1_CN_DeliveryHLS2]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DeliveryHLS#hlsaudioonly
[v1_EN_DeliveryHLS2]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_DeliveryHLS#hlsaudioonly
[v1_CN_Reload]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Reload
[v1_EN_Reload]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Reload
[v1_CN_LowLatency2]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_LowLatency#gop-cache
[v1_EN_LowLatency2]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_LowLatency#gop-cache
[v1_CN_Forward]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Forward
[v1_EN_Forward]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Forward
[v1_CN_FFMPEG]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_FFMPEG
[v1_EN_FFMPEG]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_FFMPEG
[v1_CN_HTTPCallback]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPCallback
[v1_EN_HTTPCallback]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_HTTPCallback
[v1_CN_BandwidthTestTool]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_BandwidthTestTool
[v1_EN_BandwidthTestTool]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_BandwidthTestTool
[v1_CN_SampleDemo]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SampleDemo
[v1_EN_SampleDemo]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SampleDemo
[v2_CN_SrsLibrtmp]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SrsLibrtmp
[v2_EN_SrsLibrtmp]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SrsLibrtmp
[v1_CN_SrsLinuxArm]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLinuxArm
[v1_EN_SrsLinuxArm]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SrsLinuxArm
[v1_CN_LinuxService]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_LinuxService
[v1_EN_LinuxService]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_LinuxService
[v1_CN_RTMP-ATC]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_RTMP-ATC
[v1_EN_RTMP-ATC]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_RTMP-ATC
[v1_CN_HTTPApi]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_HTTPApi
[v1_EN_HTTPApi]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_HTTPApi
[v1_CN_Ingest]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Ingest
[v1_EN_Ingest]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_Ingest
[v1_CN_DVR]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DVR
[v1_EN_DVR]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_DVR
[v1_CN_SrsLog]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLog
[v1_EN_SrsLog]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_SrsLog
[v1_CN_DRM2]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DRM#tokentraverse
[v1_EN_DRM2]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_DRM#tokentraverse
[v2_CN_SampleHTTP]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SampleHTTP
[v2_EN_SampleHTTP]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SampleHTTP
[v2_CN_FlvVodStream]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_FlvVodStream
[v2_EN_FlvVodStream]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_FlvVodStream
[v2_CN_SrsLibrtmp2]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SrsLibrtmp#publish-h264-raw-data
[v2_EN_SrsLibrtmp2]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SrsLibrtmp#publish-h264-raw-data
[v2_CN_SrsLibrtmp3]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SrsLibrtmp#publish-audio-raw-stream
[v2_EN_SrsLibrtmp3]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SrsLibrtmp#publish-audio-raw-stream
[v2_CN_Security]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_Security
[v2_EN_Security]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_Security
[v2_CN_DeliveryHttpStream]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_DeliveryHttpStream
[v2_EN_DeliveryHttpStream]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_DeliveryHttpStream
[v1_CN_DeliveryHDS]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_DeliveryHDS
[v1_EN_DeliveryHDS]: https://github.com/simple-rtmp-server/srs/wiki/v1_EN_DeliveryHDS
[v2_CN_Streamer2]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_Streamer#push-http-flv-to-srs
[v2_EN_Streamer2]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_Streamer#push-http-flv-to-srs
[v2_CN_SampleHttpFlv]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SampleHttpFlv
[v2_EN_SampleHttpFlv]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SampleHttpFlv
[v2_CN_SampleHttpFlvCluster]: https://github.com/simple-rtmp-server/srs/wiki/v2_CN_SampleHttpFlvCluster
[v2_EN_SampleHttpFlvCluster]: https://github.com/simple-rtmp-server/srs/wiki/v2_EN_SampleHttpFlvCluster

[bug #213]: https://github.com/simple-rtmp-server/srs/issues/213
[bug #194]: https://github.com/simple-rtmp-server/srs/issues/194
[bug #182]: https://github.com/simple-rtmp-server/srs/issues/182
[bug #257]: https://github.com/simple-rtmp-server/srs/issues/257
[bug #179]: https://github.com/simple-rtmp-server/srs/issues/179
[bug #224]: https://github.com/simple-rtmp-server/srs/issues/224
[bug #251]: https://github.com/simple-rtmp-server/srs/issues/251
[bug #293]: https://github.com/simple-rtmp-server/srs/issues/293
[bug #250]: https://github.com/simple-rtmp-server/srs/issues/250
[bug #301]: https://github.com/simple-rtmp-server/srs/issues/301
[bug #304]: https://github.com/simple-rtmp-server/srs/issues/304
[bug #133]: https://github.com/simple-rtmp-server/srs/issues/133
[bug #92]: https://github.com/simple-rtmp-server/srs/issues/92


[contact]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Contact
[more0]: http://winlinvip.github.io/srs.release/releases/
[more1]: http://www.ossrs.net/srs.release/releases/

[branch2]: https://github.com/simple-rtmp-server/srs/tree/2.0release
[release2]: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_Product#release20
[centos0]: http://winlinvip.github.io/srs.release/releases/files/SRS-CentOS6-x86_64-1.0.32.zip
[centos1]: http://www.ossrs.net/srs.release/releases/files/SRS-CentOS6-x86_64-1.0.32.zip



