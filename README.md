#Simple-RTMP-Server

SRS定位是运营级的互联网直播服务器集群，追求更好的概念完整性和最简单实现的代码。

SRS is industrial-strength live streaming cluster, 
for the best conceptual integrity and the simplest implementation.

Download from github.io: 
[Centos6-x86_64](http://winlinvip.github.io/srs.release/releases/files/SRS-CentOS6-x86_64-1.0.0.zip) 
[more...](http://winlinvip.github.io/srs.release/releases/)

Download from ossrs.net: 
[Centos6-x86_64](http://www.ossrs.net/srs.release/releases/files/SRS-CentOS6-x86_64-1.0.0.zip) 
[more...](http://www.ossrs.net/srs.release/releases/)

## About

SRS(SIMPLE RTMP Server) over state-threads created in 2013.10.

SRS delivers rtmp/hls live on x86/x64/arm/mips linux, 
supports origin/edge/vhost and transcode/ingest and dvr/forward 
and http-api/http-callback/reload, introduces tracable 
session-oriented log, exports client srs-librtmp, 
provides EN/CN wiki and the most simple architecture.

SRS focus on small problem domain, which is the most complex for all software(see OOAD). 
Because of lack of deveoper resource, SRS only provides features which is the most popular 
for internet. SRS is simple for and only for problem domain is simplified.

SRS is a simple, RTMP(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryRTMP), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryRTMP)
),
HLS(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryHLS), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryHLS)
), 
high-performance(7.5k+ clients)(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Performance), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Performance)
),
single processes, edge/origin live server, 
x86/x64/arm(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SrsLinuxArm), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SrsLinuxArm)
),
compile depends on st(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Architecture), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Architecture)
)(required),
[ssl](http://www.openssl.org/) and [http-parser](https://github.com/joyent/http-parser), 
use [nginx](http://nginx.org/), [ffmpeg](http://ffmpeg.org/) and 
[cherrypy](http://www.cherrypy.org/) as external tools. that is, only need st to run srs for 
minimum run. see Build(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Build), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Build)
).

SRS supports vhost(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RtmpUrlVhost), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_RtmpUrlVhost)
),
rtmp(encoder push(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryRTMP), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryRTMP)
),
client/edge(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Edge),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Edge),
) pull), 
ingester(srs pull)(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Ingest), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Ingest)
),
HLS(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryHLS), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryHLS)
),
HLS audio only(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryHLS#hlsaudioonly), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryHLS#hlsaudioonly)
),
transcoding(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FFMPEG), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_FFMPEG)
),
forward(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FFMPEG), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_FFMPEG)
),
http hooks(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPCallback), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_HTTPCallback)
),
http api(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPApi), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_HTTPApi)
),
http server(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPServer),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_HTTPServer)
),
dvr(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DVR), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DVR)
) and
SRS-librtmp(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp)
).

SRS-librtmp(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp)
)
is a client library, only depends on c++ and socket, with 
examples(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp#srs-librtmp-examples),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp#srs-librtmp-examples)
)(to play, 
publish, ingest flv/rtmp, inject flv, 
publish h264 raw stream(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp#publish-h264-raw-data), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp#publish-h264-raw-data)
),
exported as seperate project or single cpp file by configure(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp#export-srs-librtmp),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp#export-srs-librtmp)
).
SRS-librtmp(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp)
)
provides apis to support RTMP, FLV, AMF0 and 
h.264 raw stream(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp#publish-h264-raw-data),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp#publish-h264-raw-data)
).

Report Bug: [https://github.com/winlinvip/simple-rtmp-server/issues/new](https://github.com/winlinvip/simple-rtmp-server/issues/new)  <br/>
WebSite: [http://ossrs.net](http://ossrs.net) <br/>
Release: [http://winlinvip.github.io/srs.release](http://winlinvip.github.io/srs.release)  <br/>
Blog: [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin)  <br/>
QQ Group: 365936885, by wenjie <br/>
Wiki: [https://github.com/winlinvip/simple-rtmp-server/wiki](https://github.com/winlinvip/simple-rtmp-server/wiki)  <br/>
StreamServers：[BLS](https://github.com/wenjiegit/Bull-Live-Server)/[BLE](https://github.com/wenjiegit/Bull-Live-Encoder), 
[NGINX-RTMP](https://github.com/arut/nginx-rtmp-module), [CRTMPD](http://www.rtmpd.com/), 
[RED5](http://www.red5.org/), [WOWZA](http://www.wowza.com/), 
[FMS/AMS](http://www.adobe.com/products/adobe-media-server-standard.html)

## AUTHORS

There are three types of people that have contributed to the SRS project:
* PRIMARY: Contribute important features and >10% code. Names of all 
PRIMARY response in NetConnection.connect and metadata. 
* AUTHORS: Contribute features and 1%~10% code. Names of all 
PRIMARY response in NetConnection.connect and metadata. 
* CONTRIBUTORS: Submit patches, report bugs, add translations, help answer 
newbie questions, and generally make SRS that much better.

About all PRIMARY, AUTHORS and CONTRIBUTORS, read 
[AUTHORS.txt](https://github.com/winlinvip/simple-rtmp-server/blob/master/AUTHORS.txt).

A big THANK YOU goes to:
* [chnvideo](chnvideo.com) co-founders([wiseyoung](mailto:wiseyoung@chnvideo.com), [trueice](mailto:trueice@chnvideo.com), [leijian](mailto:leijian@chnvideo.com)) for [big supports](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Product#bigthanks).
* Genes amd Mabbott for creating [st](https://github.com/winlinvip/state-threads)([state-threads](http://sourceforge.net/projects/state-threads/)).
* Michael Talyanksy for introducing us to use st.
* Roman Arutyunyan for creating [nginx-rtmp](https://github.com/arut/nginx-rtmp-module) for SRS to refer to. 
* Joyent for creating [http-parser](https://github.com/joyent/http-parser) for http-api for SRS.
* Igor Sysoev for creating [nginx](http://nginx.org/) for SRS to refer to.
* [FFMPEG](http://ffmpeg.org/) and [libx264](http://www.videolan.org/) group for SRS to use to transcode.
* Guido van Rossum for creating Python for api-server for SRS.

## Mirrors

Github: [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server),
the GIT usage(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Git), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Git)
)

```bash
git clone https://github.com/winlinvip/simple-rtmp-server.git
```

CSDN: [https://code.csdn.net/winlinvip/srs-csdn](https://code.csdn.net/winlinvip/srs-csdn) ,
the GIT usage(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Git), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Git)
)

```bash
git clone https://code.csdn.net/winlinvip/srs-csdn.git
```

OSChina: [http://git.oschina.net/winlinvip/srs.oschina](http://git.oschina.net/winlinvip/srs.oschina) ,
the GIT usage(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Git), 
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Git)
)

```bash
git clone https://git.oschina.net/winlinvip/srs.oschina.git
```

## Usage

<strong>Step 1:</strong> get SRS 

<pre>
git clone https://github.com/winlinvip/simple-rtmp-server &&
cd simple-rtmp-server/trunk
</pre>

<strong>Step 2:</strong> build SRS,
<strong>Requires Centos6.x/Ubuntu12 32/64bits, others see Build(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_Build),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_Build)
).</strong>

<pre>
./configure && make
</pre>

<strong>Step 3:</strong> start SRS 

<pre>
./objs/srs -c conf/srs.conf
</pre>

<strong>See also:</strong>
* Usage: How to delivery RTMP?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleRTMP),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleRTMP)
)
* Usage: How to delivery HLS?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleHLS),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleHLS)
)
* Usage: How to delivery HLS for other codec?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleTranscode2HLS),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleTranscode2HLS)
)
* Usage: How to transode RTMP stream by SRS?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleFFMPEG),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleFFMPEG)
)
* Usage: How to forward stream to other server?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleForward),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleForward)
)
* Usage: How to deploy low lantency application?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleRealtime),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleRealtime)
)
* Usage: How to deploy SRS on ARM?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleARM),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleARM)
)
* Usage: How to ingest file/stream/device to SRS?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleIngest),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleIngest)
)
* Usage: How to use SRS-HTTP-server to delivery HTTP/HLS stream?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleHTTP),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleHTTP)
)
* Usage: How to show the demo of SRS?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleDemo),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleDemo)
)
* Usage: How to publish h.264 raw stream to SRS?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp#publish-h264-raw-data),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp#publish-h264-raw-data)
)
* Usage: Solution using SRS?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Sample),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Sample)
)
* Usage: Why SRS?(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Product),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Product)
)

## Wiki

SRS 1.0 wiki

Please select your language:
* [SRS 1.0 English](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Home)
* [SRS 1.0 Chinese](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Home)

SRS 2.0 wiki

Please select your language:
* [SRS 2.0 English](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_Home)
* [SRS 2.0 Chinese](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_Home)

## Donation

Donation:<br/>
[http://winlinvip.github.io/srs.release/donation/index.html](http://winlinvip.github.io/srs.release/donation/index.html) OR <br/>
[http://www.ossrs.net/srs.release/donation/index.html](http://www.ossrs.net/srs.release/donation/index.html)

Donations:<br/>
[https://github.com/winlinvip/simple-rtmp-server/blob/master/DONATIONS.txt]
(https://github.com/winlinvip/simple-rtmp-server/blob/master/DONATIONS.txt)

## System Requirements
Supported operating systems and hardware:
* All Linux , both 32 and 64 bits
* All hardware.

## Summary
1. Simple, also stable enough.
1. High-performance(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Performance),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Performance)
): single-thread, async socket, event/st-thread driven.
1. High-concurrency(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Performance),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Performance)
), 6000+ connections(500kbps), 900Mbps, CPU 90.2%, 41MB
1. Support RTMP Origin Server(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryRTMP),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryRTMP)
)
1. Support RTMP Edge Server(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Edge),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Edge)
) for CDN, push/pull stream from any RTMP server
1. Support single process; no multiple processes.
1. Support Vhost(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RtmpUrlVhost),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_RtmpUrlVhost)
), support \_\_defaultVhost\_\_.
1. Support RTMP(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryRTMP),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryRTMP)
) live streaming; no vod streaming.
1. Support Apple HLS(m3u8)(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryHLS),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryHLS)
) live streaming.
1. Support HLS audio-only(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryHLS#hlsaudioonly),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DeliveryHLS#hlsaudioonly)
) live streaming.
1. Support Reload(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Reload),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Reload)
) config to enable changes.
1. Support cache last gop(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_LowLatency#gop-cache),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_LowLatency#gop-cache)
) for flash player to fast startup.
1. Support listen at multiple ports.
1. Support long time(>4.6hours) publish/play.
1. Support Forward(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Forward),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Forward)
) in master-slave mode.
1. Support live stream Transcoding(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FFMPEG),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_FFMPEG)
) by ffmpeg.
1. Support ffmpeg(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FFMPEG),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_FFMPEG)
) filters(logo/overlay/crop), x264 params, copy/vn/an.
1. Support audio transcode(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FFMPEG),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_FFMPEG)
) only, speex/mp3 to aac
1. Support http callback api hooks(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPCallback),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_HTTPCallback)
)(for authentication and injection).
1. Support bandwidth test(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_BandwidthTestTool),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_BandwidthTestTool)
) api and flash client.
1. Player, publisher(encoder), and demo pages(jquery+bootstrap)(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleDemo),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleDemo)
). 
1. Demo(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleDemo),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleDemo)
) video meeting or chat(SRS+cherrypy+jquery+bootstrap). 
1. Full documents in wiki(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Home),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Home)
), both Chinese and English. 
1. Support RTMP(play-publish) library: srs-librtmp(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp)
)
1. Support ARM cpu arch(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SrsLinuxArm),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SrsLinuxArm)
) with rtmp/ssl/hls/librtmp.
1. Support init.d(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_LinuxService),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_LinuxService)
) and packge script, log to file. 
1. Support RTMP ATC(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RTMP-ATC),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_RTMP-ATC)
) for HLS/HDS to support backup(failover)
1. Support HTTP RESTful management api(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPApi),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_HTTPApi)
).
1. Support Ingest(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Ingest),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Ingest)
) FILE/HTTP/RTMP/RTSP(RTP, SDP) to RTMP using external tools(e.g ffmepg).
1. Support DVR(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DVR),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DVR)
), record live to flv file for vod.
1. Support tracable log, session based log(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SrsLog),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SrsLog)
).
1. Support DRM token traverse(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DRM#tokentraverse),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_DRM#tokentraverse)
) for fms origin authenticate.
1. Support system full utest on gtest.
1. [experiment] Support embeded HTTP server(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleHTTP),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_SampleHTTP)
) for hls(live/vod)
1. [experiment] Support vod stream(http flv/hls vod stream)(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FlvVodStream),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_FlvVodStream)
).
1. Stable [1.0release branch](https://github.com/winlinvip/simple-rtmp-server/tree/1.0release) and 
[2.0dev branch](https://github.com/winlinvip/simple-rtmp-server/tree/master).
1. Support publish h264 raw stream(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp#publish-h264-raw-data),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp#publish-h264-raw-data)
) by srs-librtmp.
1. Support [6k+ clients](https://github.com/winlinvip/simple-rtmp-server/issues/194), 3Gbps per process.
1. Suppport [English wiki](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_EN_Home).
1. Research and simplify st, [bug #182](https://github.com/winlinvip/simple-rtmp-server/issues/182).
1. Support compile [srs-librtmp on windows](https://github.com/winlinvip/srs.librtmp), 
[bug #213](https://github.com/winlinvip/simple-rtmp-server/issues/213).
1. Support [7.5k+ clients](https://github.com/winlinvip/simple-rtmp-server/issues/217), 4Gbps per process.
1. Support publish aac adts raw stream(
[CN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_CN_SrsLibrtmp#publish-audio-raw-stream),
[EN](https://github.com/winlinvip/simple-rtmp-server/wiki/v2_EN_SrsLibrtmp#publish-audio-raw-stream)
) by srs-librtmp.
1. [no-plan] Support <500ms latency, FRSC(Fast RTMP-compatible Stream Channel tech).
1. [no-plan] Support RTMP 302 redirect [#92](https://github.com/winlinvip/simple-rtmp-server/issues/92).
1. [no-plan] Support multiple processes, for both origin and edge
1. [no-plan] Support adobe RTMFP(flash p2p) protocol.
1. [no-plan] Support adobe flash refer/token/swf verification.
1. [no-plan] Support adobe amf3 codec.
1. [no-plan] Support encryption: RTMPE/RTMPS, HLS DRM
1. [no-plan] Support RTMPT, http to tranverse firewalls
1. [no-plan] Support file source, transcoding file to live stream
1. [no-plan] Support RTP/RTSP server.

## Releases
* 2014-10-09, [Release v1.0-beta](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.beta), all bug fixed, 1.0.0, 59316 lines.<br/>
* 2014-08-03, [Release v1.0-mainline7](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline7), config utest, all bug fixed. 57432 lines.<br/>
* 2014-07-13, [Release v1.0-mainline6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline6), core/kernel/rtmp utest, refine bandwidth(as/js/srslibrtmp library). 50029 lines.<br/>
* 2014-06-27, [Release v1.0-mainline5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline5), refine perf 3k+ clients, edge token traverse, [srs monitor](http://ossrs.net:1977), 30days online. 41573 lines.<br/>
* 2014-05-28, [Release v1.0-mainline4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline4), support heartbeat, tracable log, fix mem leak and bugs. 39200 lines.<br/>
* 2014-05-18, [Release v1.0-mainline3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline3), support mips, fms origin, json(http-api). 37594 lines.<br/>
* 2014-04-28, [Release v1.0-mainline2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline2), support [dvr](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DVR), android, [edge](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Edge). 35255 lines.<br/>
* 2014-04-07, [Release v1.0-mainline](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline), support [arm](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SrsLinuxArm), [init.d](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_LinuxService), http [server](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPServer)/[api](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPApi), [ingest](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleIngest). 30000 lines.<br/>
* 2013-12-25, [Release v0.9](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.9), support bandwidth test, player/encoder/chat [demos](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleDemo). 20926 lines.<br/>
* 2013-12-08, [Release v0.8](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.8), support [http hooks callback](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPCallback), update [st_load](https://github.com/winlinvip/st-load). 19186 lines.<br/>
* 2013-12-03, [Release v0.7](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.7), support [live stream transcoding](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FFMPEG). 17605 lines.<br/>
* 2013-11-29, [Release v0.6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.6), support [forward](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Forward) stream to origin/edge. 16094 lines.<br/>
* 2013-11-26, [Release v0.5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.5), support [HLS(m3u8)](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryHLS), fragment and window. 14449 lines.<br/>
* 2013-11-10, [Release v0.4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.4), support [reload](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Reload) config, pause, longtime publish/play. 12500 lines.<br/>
* 2013-11-04, [Release v0.3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.3), support [vhost](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RtmpUrlVhost), refer, gop cache, listen multiple ports. 11773 lines.<br/>
* 2013-10-25, [Release v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2), support [rtmp](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RTMPHandshake) flash publish, h264, time jitter correct. 10125 lines.<br/>
* 2013-10-23, [Release v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1), support [rtmp FMLE/FFMPEG publish](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryRTMP), vp6. 8287 lines.<br/>
* 2013-10-17, Created.<br/>

## History
* v2.0, 2014-12-04, for [#241](https://github.com/winlinvip/simple-rtmp-server/issues/241), support mr(merged-read) config and reload. 2.0.52.
* v2.0, 2014-12-04, enable [#241](https://github.com/winlinvip/simple-rtmp-server/issues/241) and [#248](https://github.com/winlinvip/simple-rtmp-server/issues/248), +25% performance, 2.5k publisher. 2.0.50
* v2.0, 2014-12-04, fix [#248](https://github.com/winlinvip/simple-rtmp-server/issues/248), improve about 15% performance for fast buffer. 2.0.49
* v2.0, 2014-12-03, fix [#244](https://github.com/winlinvip/simple-rtmp-server/issues/244), conn thread use cond to wait for recv thread error. 2.0.47.
* v2.0, 2014-12-02, merge [#239](https://github.com/winlinvip/simple-rtmp-server/pull/239), traverse the token before response connect. 2.0.45.
* v2.0, 2014-12-02, srs-librtmp support hijack io apis for st-load. 2.0.42.
* v2.0, 2014-12-01, for [#237](https://github.com/winlinvip/simple-rtmp-server/issues/237), refine syscall for recv, supports 1.5k clients. 2.0.41.
* v2.0, 2014-11-30, add qtcreate project file trunk/src/qt/srs/srs-qt.pro. 2.0.39.
* v2.0, 2014-11-29, fix [#235](https://github.com/winlinvip/simple-rtmp-server/issues/235), refine handshake, replace union with template method. 2.0.38.
* v2.0, 2014-11-28, fix [#215](https://github.com/winlinvip/simple-rtmp-server/issues/215), add srs_rtmp_dump tool. 2.0.37.
* v2.0, 2014-11-25, update PRIMARY, AUTHORS, CONTRIBUTORS rule. 2.0.32.
* v2.0, 2014-11-24, fix [#212](https://github.com/winlinvip/simple-rtmp-server/issues/212), support publish aac adts raw stream. 2.0.31.
* v2.0, 2014-11-22, fix [#217](https://github.com/winlinvip/simple-rtmp-server/issues/217), remove timeout recv, support 7.5k+ 250kbps clients. 2.0.30.
* v2.0, 2014-11-21, srs-librtmp add rtmp prefix for rtmp/utils/human apis. 2.0.29.
* v2.0, 2014-11-21, refine examples of srs-librtmp, add srs_print_rtmp_packet. 2.0.28.
* v2.0, 2014-11-20, fix [#212](https://github.com/winlinvip/simple-rtmp-server/issues/212), support publish audio raw frames. 2.0.27
* v2.0, 2014-11-19, fix [#213](https://github.com/winlinvip/simple-rtmp-server/issues/213), support compile [srs-librtmp on windows](https://github.com/winlinvip/srs.librtmp), [bug #213](https://github.com/winlinvip/simple-rtmp-server/issues/213). 2.0.26
* v2.0, 2014-11-18, all wiki translated to English. 2.0.23.
* v2.0, 2014-11-15, fix [#204](https://github.com/winlinvip/simple-rtmp-server/issues/204), srs-librtmp drop duplicated sps/pps(sequence header). 2.0.22.
* v2.0, 2014-11-15, fix [#203](https://github.com/winlinvip/simple-rtmp-server/issues/203), srs-librtmp drop any video before sps/pps(sequence header). 2.0.21.
* v2.0, 2014-11-15, fix [#202](https://github.com/winlinvip/simple-rtmp-server/issues/202), fix memory leak of h.264 raw packet send in srs-librtmp. 2.0.20.
* v2.0, 2014-11-13, fix [#200](https://github.com/winlinvip/simple-rtmp-server/issues/200), deadloop when read/write 0 and ETIME. 2.0.16.
* v2.0, 2014-11-13, fix [#194](https://github.com/winlinvip/simple-rtmp-server/issues/194), writev multiple msgs, support 6k+ 250kbps clients. 2.0.15.
* v2.0, 2014-11-12, fix [#194](https://github.com/winlinvip/simple-rtmp-server/issues/194), optmized st for timeout recv. pulse to 500ms. 2.0.14.
* v2.0, 2014-11-11, fix [#195](https://github.com/winlinvip/simple-rtmp-server/issues/195), remove the confuse code st_usleep(0). 2.0.13.
* v2.0, 2014-11-08, fix [#191](https://github.com/winlinvip/simple-rtmp-server/issues/191), configure --export-librtmp-project and --export-librtmp-single. 2.0.11.
* v2.0, 2014-11-08, fix [#66](https://github.com/winlinvip/simple-rtmp-server/issues/66), srs-librtmp support write h264 raw packet. 2.0.9.
* v2.0, 2014-10-25, fix [#185](https://github.com/winlinvip/simple-rtmp-server/issues/185), AMF0 support 0x0B the date type codec. 2.0.7.
* v2.0, 2014-10-24, fix [#186](https://github.com/winlinvip/simple-rtmp-server/issues/186), hotfix for bug #186, drop connect args when not object. 2.0.6.
* v2.0, 2014-10-24, rename wiki/xxx to wiki/v1_CN_xxx. 2.0.3.
* v2.0, 2014-10-19, fix [#184](https://github.com/winlinvip/simple-rtmp-server/issues/184), support AnnexB in RTMP body for HLS. 2.0.2
* v2.0, 2014-10-18, remove supports for OSX(darwin). 2.0.1.
* v2.0, 2014-10-16, revert github srs README to English. 2.0.0.
* <strong>v1.0, 2014-10-09, [1.0 beta(1.0.0)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.beta) released. 59316 lines.</strong>
* v1.0, 2014-10-08, fix [#151](https://github.com/winlinvip/simple-rtmp-server/issues/151), always reap ts whatever audio or video packet. 0.9.223.
* v1.0, 2014-10-08, fix [#162](https://github.com/winlinvip/simple-rtmp-server/issues/162), failed if no epoll. 0.9.222.
* v1.0, 2014-09-30, fix [#180](https://github.com/winlinvip/simple-rtmp-server/issues/180), crash for multiple edge publishing the same stream. 0.9.220.
* v1.0, 2014-09-26, fix hls bug, refine config and log, according to clion of jetbrains. 0.9.216. 
* v1.0, 2014-09-25, fix [#177](https://github.com/winlinvip/simple-rtmp-server/issues/177), dvr segment add config dvr_wait_keyframe. 0.9.213.
* v1.0, 2014-08-28, fix [#167](https://github.com/winlinvip/simple-rtmp-server/issues/167), add openssl includes to utest. 0.9.209.
* v1.0, 2014-08-27, max connections is 32756, for st use mmap default. 0.9.209
* v1.0, 2014-08-24, fix [#150](https://github.com/winlinvip/simple-rtmp-server/issues/150), forward should forward the sequence header when retry. 0.9.208.
* v1.0, 2014-08-22, for [#165](https://github.com/winlinvip/simple-rtmp-server/issues/165), refine dh wrapper, ensure public key is 128bytes. 0.9.206.
* v1.0, 2014-08-19, for [#160](https://github.com/winlinvip/simple-rtmp-server/issues/160), support forward/edge to flussonic, disable debug_srs_upnode to make flussonic happy. 0.9.201.
* v1.0, 2014-08-17, for [#155](https://github.com/winlinvip/simple-rtmp-server/issues/155), refine for osx, with ssl/http, disable statistics. 0.9.198.
* v1.0, 2014-08-06, fix [#148](https://github.com/winlinvip/simple-rtmp-server/issues/148), simplify the RTMP handshake key generation. 0.9.191.
* v1.0, 2014-08-06, fix [#147](https://github.com/winlinvip/simple-rtmp-server/issues/147), support identify the srs edge. 0.9.190.
* <strong>v1.0, 2014-08-03, [1.0 mainline7(0.9.189)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline7) released. 57432 lines.</strong>
* v1.0, 2014-08-03, fix [#79](https://github.com/winlinvip/simple-rtmp-server/issues/79), fix the reload remove edge assert bug. 0.9.189.
* v1.0, 2014-08-03, fix [#57](https://github.com/winlinvip/simple-rtmp-server/issues/57), use lock(acquire/release publish) to avoid duplicated publishing. 0.9.188.
* v1.0, 2014-08-03, fix [#85](https://github.com/winlinvip/simple-rtmp-server/issues/85), fix the segment-dvr sequence header missing. 0.9.187.
* v1.0, 2014-08-03, fix [#145](https://github.com/winlinvip/simple-rtmp-server/issues/145), refine ffmpeg log, check abitrate for libaacplus. 0.9.186.
* v1.0, 2014-08-03, fix [#143](https://github.com/winlinvip/simple-rtmp-server/issues/143), fix retrieve sys stat bug for all linux. 0.9.185.
* v1.0, 2014-08-02, fix [#138](https://github.com/winlinvip/simple-rtmp-server/issues/138), fix http hooks bug, regression bug. 0.9.184.
* v1.0, 2014-08-02, fix [#142](https://github.com/winlinvip/simple-rtmp-server/issues/142), fix tcp stat slow bug, use /proc/net/sockstat instead, refer to 'ss -s'. 0.9.183.
* v1.0, 2014-07-31, fix [#141](https://github.com/winlinvip/simple-rtmp-server/issues/141), support tun0(vpn network device) ip retrieve. 0.9.179.
* v1.0, 2014-07-27, support partially build on OSX(Darwin). 0.9.177
* v1.0, 2014-07-27, api connections add udp, add disk iops. 0.9.176
* v1.0, 2014-07-26, complete config utest. 0.9.173
* v1.0, 2014-07-26, fix [#124](https://github.com/winlinvip/simple-rtmp-server/issues/124), gop cache support disable video in publishing. 0.9.171.
* v1.0, 2014-07-23, fix [#121](https://github.com/winlinvip/simple-rtmp-server/issues/121), srs_info detail log compile failed. 0.9.168.
* v1.0, 2014-07-19, fix [#119](https://github.com/winlinvip/simple-rtmp-server/issues/119), use iformat and oformat for ffmpeg transcode. 0.9.163.
* <strong>v1.0, 2014-07-13, [1.0 mainline6(0.9.160)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline6) released. 50029 lines.</strong>
* v1.0, 2014-07-13, refine the bandwidth check/test, add as/js library, use srs-librtmp for linux tool. 0.9.159
* v1.0, 2014-07-12, complete rtmp stack utest. 0.9.156
* v1.0, 2014-07-06, fix [#81](https://github.com/winlinvip/simple-rtmp-server/issues/81), fix HLS codec info, IOS ok. 0.9.153.
* v1.0, 2014-07-06, fix [#103](https://github.com/winlinvip/simple-rtmp-server/issues/103), support all aac sample rate. 0.9.150.
* v1.0, 2014-07-05, complete kernel utest. 0.9.149
* v1.0, 2014-06-30, fix [#111](https://github.com/winlinvip/simple-rtmp-server/issues/111), always use 31bits timestamp. 0.9.143.
* v1.0, 2014-06-28, response the call message with null. 0.9.137
* v1.0, 2014-06-28, fix [#110](https://github.com/winlinvip/simple-rtmp-server/issues/110), thread start segment fault, thread cycle stop destroy thread. 0.9.136
* v1.0, 2014-06-27, fix [#109](https://github.com/winlinvip/simple-rtmp-server/issues/109), fix the system jump time, adjust system startup time. 0.9.135
* <strong>v1.0, 2014-06-27, [1.0 mainline5(0.9.134)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline5) released. 41573 lines.</strong>
* v1.0, 2014-06-27, SRS online 30days with RTMP/HLS.
* v1.0, 2014-06-25, fix [#108](https://github.com/winlinvip/simple-rtmp-server/issues/108), support config time jitter for encoder non-monotonical stream. 0.9.133
* v1.0, 2014-06-23, support report summaries in heartbeat. 0.9.132
* v1.0, 2014-06-22, performance refine, support [3k+](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Performance#%E6%80%A7%E8%83%BD%E4%BE%8B%E8%A1%8C%E6%8A%A5%E5%91%8A4k) connections(270kbps). 0.9.130
* v1.0, 2014-06-21, support edge [token traverse](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DRM#tokentraverse), fix [#104](https://github.com/winlinvip/simple-rtmp-server/issues/104). 0.9.129
* v1.0, 2014-06-19, add connections count to api summaries. 0.9.127
* v1.0, 2014-06-19, add srs bytes and kbps to api summaries. 0.9.126
* v1.0, 2014-06-18, add network bytes to api summaries. 0.9.125
* v1.0, 2014-06-14, fix [#98](https://github.com/winlinvip/simple-rtmp-server/issues/98), workaround for librtmp ping(fmt=1,cid=2 fresh stream). 0.9.124
* v1.0, 2014-05-29, support flv inject and flv http streaming with start=bytes. 0.9.122
* <strong>v1.0, 2014-05-28, [1.0 mainline4(0.9.120)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline4) released. 39200 lines.</strong>
* v1.0, 2014-05-27, fix [#87](https://github.com/winlinvip/simple-rtmp-server/issues/87), add source id for full trackable log. 0.9.120
* v1.0, 2014-05-27, fix [#84](https://github.com/winlinvip/simple-rtmp-server/issues/84), unpublish when edge disconnect. 0.9.119
* v1.0, 2014-05-27, fix [#89](https://github.com/winlinvip/simple-rtmp-server/issues/89), config to /dev/null to disable ffmpeg log. 0.9.117
* v1.0, 2014-05-25, fix [#76](https://github.com/winlinvip/simple-rtmp-server/issues/76), allow edge vhost to add or remove. 0.9.114
* v1.0, 2014-05-24, Johnny contribute [ossrs.net](http://ossrs.net). karthikeyan start to translate wiki to English.
* v1.0, 2014-05-22, fix [#78](https://github.com/winlinvip/simple-rtmp-server/issues/78), st joinable thread must be stop by other threads, 0.9.113
* v1.0, 2014-05-22, support amf0 StrictArray(0x0a). 0.9.111.
* v1.0, 2014-05-22, support flv parser, add amf0 to librtmp. 0.9.110
* v1.0, 2014-05-22, fix [#74](https://github.com/winlinvip/simple-rtmp-server/issues/74), add tcUrl for http callback on_connect, 0.9.109
* v1.0, 2014-05-19, support http heartbeat, 0.9.107
* <strong>v1.0, 2014-05-18, [1.0 mainline3(0.9.105)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline3) released. 37594 lines.</strong>
* v1.0, 2014-05-18, support http api json, to PUT/POST. 0.9.105
* v1.0, 2014-05-17, fix [#72](https://github.com/winlinvip/simple-rtmp-server/issues/72), also need stream_id for send_and_free_message. 0.9.101
* v1.0, 2014-05-17, rename struct to class. 0.9.100
* v1.0, 2014-05-14, fix [#67](https://github.com/winlinvip/simple-rtmp-server/issues/67) pithy print, stage must has a age. 0.9.98
* v1.0, 2014-05-13, fix mem leak for delete[] SharedPtrMessage array. 0.9.95
* v1.0, 2014-05-12, refine the kbps calc module. 0.9.93
* v1.0, 2014-05-12, fix bug [#64](https://github.com/winlinvip/simple-rtmp-server/issues/64): install_dir=DESTDIR+PREFIX
* v1.0, 2014-05-08, fix [#36](https://github.com/winlinvip/simple-rtmp-server/issues/36): never directly use \*(int32_t\*) for arm.
* v1.0, 2014-05-08, fix [#60](https://github.com/winlinvip/simple-rtmp-server/issues/60): support aggregate message
* v1.0, 2014-05-08, fix [#59](https://github.com/winlinvip/simple-rtmp-server/issues/59), edge support FMS origin server. 0.9.92
* v1.0, 2014-05-06, fix [#50](https://github.com/winlinvip/simple-rtmp-server/issues/50), ubuntu14 build error.
* v1.0, 2014-05-04, support mips linux.
* v1.0, 2014-04-30, fix bug [#34](https://github.com/winlinvip/simple-rtmp-server/issues/34): convert signal to io thread. 0.9.85
* v1.0, 2014-04-29, refine RTMP protocol completed, to 0.9.81
* <strong>v1.0, 2014-04-28, [1.0 mainline2(0.9.79)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline2) released. 35255 lines.</strong>
* v1.0, 2014-04-28, support full edge RTMP server. 0.9.79
* v1.0, 2014-04-27, support basic edge(play/publish) RTMP server. 0.9.78
* v1.0, 2014-04-25, add donation page. 0.9.76
* v1.0, 2014-04-21, support android app to start srs for internal edge. 0.9.72
* v1.0, 2014-04-19, support tool over srs-librtmp to ingest flv/rtmp. 0.9.71
* v1.0, 2014-04-17, support dvr(record live to flv file for vod). 0.9.69
* v1.0, 2014-04-11, add speex1.2 to transcode flash encoder stream. 0.9.58
* v1.0, 2014-04-10, support reload ingesters(add/remov/update). 0.9.57
* <strong>v1.0, 2014-04-07, [1.0 mainline(0.9.55)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline) released. 30000 lines.</strong>
* v1.0, 2014-04-07, support [ingest](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleIngest) file/stream/device.
* v1.0, 2014-04-05, support [http api](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPApi) and [http server](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPServer).
* v1.0, 2014-04-03, implements http framework and api/v1/version.
* v1.0, 2014-03-30, fix bug for st detecting epoll failed, force st to use epoll.
* v1.0, 2014-03-29, add wiki [Performance for RaspberryPi](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RaspberryPi).
* v1.0, 2014-03-29, add release binary package for raspberry-pi. 
* v1.0, 2014-03-26, support RTMP ATC for HLS/HDS to support backup(failover).
* v1.0, 2014-03-23, support daemon, default start in daemon.
* v1.0, 2014-03-22, support make install/install-api and uninstall.
* v1.0, 2014-03-22, add ./etc/init.d/srs, refine to support make clean then make.
* v1.0, 2014-03-21, write pid to ./objs/srs.pid.
* v1.0, 2014-03-20, refine hls code, support pure audio HLS.
* v1.0, 2014-03-19, add vn/an for FFMPEG to drop video/audio for radio stream.
* v1.0, 2014-03-19, refine handshake, client support complex handshake, add utest.
* v1.0, 2014-03-16, fix bug on arm of st, the sp change from 20 to 8, for respberry-pi, @see [commit](https://github.com/winlinvip/simple-rtmp-server/commit/5a4373d4835758188b9a1f03005cea0b6ddc62aa)
* v1.0, 2014-03-16, support ARM([debian armhf, v7cpu](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SrsLinuxArm)) with rtmp/ssl/hls/librtmp.
* v1.0, 2014-03-12, finish utest for amf0 codec.
* v1.0, 2014-03-06, add gperftools for mem leak detect, mem/cpu profile.
* v1.0, 2014-03-04, add gest framework for utest, build success.
* v1.0, 2014-03-02, add wiki [srs-librtmp](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SrsLibrtmp), [SRS for arm](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SrsLinuxArm), [product](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Product)
* v1.0, 2014-03-02, srs-librtmp, client publish/play library like librtmp.
* v1.0, 2014-03-01, modularity, extract core/kernel/rtmp/app/main module.
* v1.0, 2014-02-28, support arm build(SRS/ST), add ssl to 3rdparty package.
* v1.0, 2014-02-28, add wiki [BuildArm](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Build), [FFMPEG](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_FFMPEG), [Reload](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Reload)
* v1.0, 2014-02-27, add wiki [LowLatency](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_LowLatency), [HTTPCallback](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPCallback), [ServerSideScript](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_ServerSideScript), [IDE](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_IDE)
* v1.0, 2014-01-19, add wiki [DeliveryHLS](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_DeliveryHLS)
* v1.0, 2014-01-12, add wiki [HowToAskQuestion](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HowToAskQuestion), [RtmpUrlVhost](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RtmpUrlVhost)
* v1.0, 2014-01-11, fix jw/flower player pause bug, which send closeStream actually.
* v1.0, 2014-01-05, add wiki [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Build), [Performance](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Performance), [Forward](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Forward)
* v1.0, 2014-01-01, change listen(512), chunk-size(60000), to improve performance.
* v1.0, 2013-12-27, merge from wenjie, the bandwidth test feature.
* <strong>v0.9, 2013-12-25, [v0.9](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.9) released. 20926 lines.</strong>
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
* <strong>v0.8, 2013-12-08, [v0.8](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.8) released. 19186 lines.</strong>
* v0.8, 2013-12-08, support [http hooks](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_HTTPCallback): on_connect/close/publish/unpublish/play/stop.
* v0.8, 2013-12-08, support multiple http hooks for a event.
* v0.8, 2013-12-07, support http callback hooks, on_connect.
* v0.8, 2013-12-07, support network based cli and json result, add CherryPy 3.2.4.
* v0.8, 2013-12-07, update http/hls/rtmp load test tool [st_load](https://github.com/winlinvip/st-load), use SRS rtmp sdk.
* v0.8, 2013-12-06, support max_connections, drop if exceed.
* v0.8, 2013-12-05, support log_dir, write ffmpeg log to file.
* v0.8, 2013-12-05, fix the forward/hls/encoder bug.
* <strong>v0.7, 2013-12-03, [v0.7](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.7) released. 17605 lines.</strong>
* v0.7, 2013-12-01, support dead-loop detect for forwarder and transcoder.
* v0.7, 2013-12-01, support all ffmpeg filters and params.
* v0.7, 2013-11-30, support live stream transcoder by ffmpeg.
* v0.7, 2013-11-30, support --with/without -ffmpeg, build ffmpeg-2.1.
* v0.7, 2013-11-30, add ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
* <strong>v0.6, 2013-11-29, [v0.6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.6) released. 16094 lines.</strong>
* v0.6, 2013-11-29, add performance summary, 1800 clients, 900Mbps, CPU 90.2%, 41MB.
* v0.6, 2013-11-29, support forward stream to other edge server.
* v0.6, 2013-11-29, support forward stream to other origin server.
* v0.6, 2013-11-28, fix memory leak bug, aac decode bug.
* v0.6, 2013-11-27, support --with or --without -hls and -ssl options.
* v0.6, 2013-11-27, support AAC 44100HZ sample rate for iphone, adjust the timestamp.
* <strong>v0.5, 2013-11-26, [v0.5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.5) released. 14449 lines.</strong>
* v0.5, 2013-11-24, support HLS(m3u8), fragment and window.
* v0.5, 2013-11-24, support record to ts file for HLS.
* v0.5, 2013-11-21, add ts_info tool to demux ts file.
* v0.5, 2013-11-16, add rtmp players(OSMF/jwplayer5/jwplayer6).
* <strong>v0.4, 2013-11-10, [v0.4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.4) released. 12500 lines.</strong>
* v0.4, 2013-11-10, support config and reload the pithy print.
* v0.4, 2013-11-09, support reload config(vhost and its detail).
* v0.4, 2013-11-09, support reload config(listen and chunk_size) by SIGHUP(1).
* v0.4, 2013-11-09, support longtime(>4.6hours) publish/play.
* v0.4, 2013-11-09, support config the chunk_size.
* v0.4, 2013-11-09, support pause for live stream.
* <strong>v0.3, 2013-11-04, [v0.3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.3) released. 11773 lines.</strong>
* v0.3, 2013-11-04, support refer/play-refer/publish-refer.
* v0.3, 2013-11-04, support vhosts specified config.
* v0.3, 2013-11-02, support listen multiple ports.
* v0.3, 2013-11-02, support config file in nginx-conf style.
* v0.3, 2013-10-29, support pithy print log message specified by stage.
* v0.3, 2013-10-28, support librtmp without extended-timestamp in 0xCX chunk packet.
* v0.3, 2013-10-27, support cache last gop for client fast startup.
* <strong>v0.2, 2013-10-25, [v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2) released. 10125 lines.</strong>
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake.
* v0.2, 2013-10-24, support time jitter detect and correct algorithm
* v0.2, 2013-10-24, support decode codec type to cache the h264/avc sequence header.
* <strong>v0.1, 2013-10-23, [v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1) released. 8287 lines.</strong>
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send\_message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv\_message).

## Performance

Performance benchmark history, on virtual box.

* See also: [Performance for x86/x64 Test Guide](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Performance)
* See also: [Performance for RaspberryPi](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RaspberryPi)

### Play benchmark

The play benchmark by [st-load](https://github.com/winlinvip/st-load):

* 2013-11-28, SRS 0.5.0,   1.8k(1800)clients, 90%CPU, 41MB.
* 2014-07-12, SRS 0.9.156, 1.8k(1800)clients, 68%CPU, 38MB.
* 2014-07-12, SRS 0.9.156, 2.7k(2700)clients, 89%CPU, 61MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/1ae3e6c64cc5cee90e6050c26968ebc3c18281be)
* 2014-11-11, SRS 1.0.5,   2.7k(2700)clients, 85%CPU, 66MB. 
* 2014-11-11, SRS 2.0.12,  2.7k(2700)clients, 85%CPU, 66MB.
* 2014-11-12, SRS 2.0.14,  2.7k(2700)clients, 69%CPU, 59MB.
* 2014-11-12, SRS 2.0.14,  3.5k(3500)clients, 95%CPU, 78MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/8acd143a7a152885b815999162660fd4e7a3f247)
* 2014-11-13, SRS 2.0.15,  6.0k(6000)clients, 82%CPU, 203MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/cc6aca9ad55342a06440ce7f3b38453776b2b2d1)
* 2014-11-22, SRS 2.0.30,  7.5k(7500)clients, 87%CPU, 320MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/58136ec178e3d47db6c90a59875d7e40946936e5)

### Publish benchmark

The publish benchmark by [st-load](https://github.com/winlinvip/st-load):

* 2014-12-03, SRS 1.0.10,  1.2k(1200) publishers, 96%CPU, 43MB.
* 2014-12-03, SRS 2.0.12,  1.2k(1200) publishers, 96%CPU, 43MB.
* 2014-12-03, SRS 2.0.47,  1.2k(1200) publishers, 84%CPU, 76MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/787ab674e38734ea8e0678101614fdcd84645dc8)
* 2014-12-03, SRS 2.0.47,  1.4k(1400) publishers, 95%CPU, 140MB.
* 2014-12-03, SRS 2.0.48,  1.4k(1400) publishers, 95%CPU, 140MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/f35ec2155b1408d528a9f37da7904c9625186bcf)
* 2014-12-04, SRS 2.0.49,  1.4k(1400) publishers, 68%CPU, 144MB.
* 2014-12-04, SRS 2.0.49,  2.5k(2500) publishers, 95%CPU, 404MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/29324fab469e0f7cef9ad04ffdbce832ac7dd9ff)
* 2014-12-04, SRS 2.0.51,  2.5k(2500) publishers, 91%CPU, 259MB. [commit](https://github.com/winlinvip/simple-rtmp-server/commit/f57801eb46c16755b173984b915a4166922df6a6)
* 2014-12-04, SRS 2.0.52,  4.0k(4000) publishers, 80%CPU, 331MB. (mr_sleep=2000) [commit](https://github.com/winlinvip/simple-rtmp-server/commit/5589b13d2e216b91f97afb78ee0c011b2fccf7da)

## Architecture

SRS always use the most simple architecture to support complex transaction.
* System arch: the system structure and arch.
* Modularity arch: the main modularity of SRS.
* Stream arch: the stream dispatch arch of SRS.
* RTMP cluster arch: the RTMP origin and edge cluster arch.
* Multiple processes arch (by wenjie): the multiple process of SRS.
* CLI arch: the cli arch for SRS, api to manage SRS.
* Bandwidth specification: the bandwidth test specification of SRS.

### System Architecture

<pre>
+------------------------------------------------------+
|             SRS(Simple RTMP Server)                  |
+---------------+---------------+-----------+----------+
|   API/hook    |   Transcoder  |    HLS    |   RTMP   |
|  http-parser  |  FFMPEG/x264  |  NGINX/ts | protocol |
+---------------+---------------+-----------+----------+
|              Network(state-threads)                  |
+------------------------------------------------------+
|      All Linux(RHEL,CentOS,Ubuntu,Fedora...)         |
+------------------------------------------------------+
</pre>

### Modularity Architecture

<pre>
+------------------------------------------------------+
|             Main(srs/bandwidth/librtmp)              |
+------------------------------------------------------+
|           App(Server/Client application)             |
+------------------------------------------------------+
|               RTMP(Protocol stack)                   |
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
|    Encoder(1)        |   +-> RTMP protocol ----+-> Flash Player |
|  (FMLE,FFMPEG, -rtmp-+->-+-> HLS/NGINX --------+-> m3u8 player  |
|  Flash,XSPLIT,       |   +-> Fowarder ---------+-> RTMP Server  |
|  ......)             |   +-> Transcoder -------+-> RTMP Server  |
|                      |   +-> DVR --------------+-> FILE         |
|                      |   +-> BandwidthTest ----+-> Flash/StLoad |
+----------------------+                         |                |
|  MediaSource(2)      |                         |                |
|  (RTSP,FILE,         |                         |                |
|   HTTP,HLS,    ------+->-- Ingester ----(rtmp)-+-> SRS          |
|   Device,            |                         |                |
|   ......)            |                         |                |
+----------------------+-------------------------+----------------+

Remark:
(1) Encoder: encoder must push RTMP stream to SRS server.
(2) MediaSource: any media source, which can be ingest by ffmpeg.
(3) Ingester: SRS will fork a process to run ffmpeg(or your application) 
to ingest any input to rtmp, push to SRS.
</pre>

### [HDS/HLS origin backup](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_RTMP-ATC)

<pre>
                        +----------+        +----------+
               +--ATC->-+  server  +--ATC->-+ packager +-+   +---------+
+----------+   | RTMP   +----------+ RTMP   +----------+ |   | Reverse |    +-------+
| encoder  +->-+                                         +->-+  Proxy  +-->-+  CDN  +
+----------+   |        +----------+        +----------+ |   | (nginx) |    +-------+
               +--ATC->-+  server  +--ATC->-+ packager +-+   +---------+
                 RTMP   +----------+ RTMP   +----------+
</pre>

### [RTMP cluster(origin/edge) Architecture](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Edge)

Remark: cluster over edge, see [Edge](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Edge)
Remark: cluster over forward, see [Forward](https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_Forward)

<pre>
+---------+       +-----------------+     +-----------------------+ 
+ Encoder +--+-->-+  SRS(RTMP Edge) +--->-+     (RTMP Origin)     | 
+---------+  |    +-----------------+     |   SRS/FMS/NGINX-RTMP  |
             |                            |    Red5/HELIX/CRTMP   |
             +-------------------------->-+         ......        |
                                          +-----------------------+ 
Schema#1: Any RTMP encoder push RTMP stream to RTMP (origin/edge)server,
    where SRS RTMP Edge server will forward stream to origin.


+-------------+    +-----------------+      +--------------------+
| RTMP Origin +-->-+  SRS(RTMP Edge) +--+->-+  Client(RTMP/HLS)  |
+-------------+    +-----------------+  |   |  Flash/IOS/Android |
                                        |   +--------------------+
                                        |
                                        |   +-----------------+
                                        +->-+  SRS(RTMP Edge) +
                                            +-----------------+
Schema#2: SRS RTMP Edge server pull stream from origin (or upstream SRS 
    RTMP Edge server), then delivery to Client.
</pre>

### (plan) SRS Multiple processes Architecture(design by wenjie)

<pre>
                 +---------------+              +--------+
                 | upnode server |              + client +
                 +-------+-------+              +---+----+
            -------------+------------network-------+---------
                         |                          |
 +--------+         +----+-----------+         +----+----------+
 | master +--fork->-+ back source(1) +-->-pull-+ stream 1-N(2) +
 +---+----+         +----------------+         +-------+-------+
     |                                                 |
     +-------------------------------------fork--->-----+
     |                           +-------------+
     +-------------------fork-->-+ http/vod(3) |
                                 +-------------+
Remark:
(1) back source process: create by master process, get stream from 
    upnode server if edge, create stream if origin, serve the stream 
    process.
(2) stream process: create by master process, get stream from back
    source process, serve the client.
(3) the embeded mininum http server, also provides vod service. for
    http server, it provides http api, hls(live/vod) delivery. for
    vod server, it slice the file to hls(m3u8/ts).
Remark:
(a) This multiple processes architecture is design by wenjie, it's a
    very simple and powerful multiple process architecture, for the
    master no need to pass between stream process.
(b) The CLI architecture is similar to this, instead, cli process
    will collect informations from all stream process, master process
    only send signals to child processes.
(c) Maybe multiple thread is ok? By winlin.
</pre>

### Bandwidth Test Workflow

<pre>
   +------------+                    +----------+
   |  Client    |                    |  Server  |
   +-----+------+                    +-----+----+
         |                                 |
         |   connect vhost------------->   |
         |   &lt;-----------result(success)   |
         |                                 |
         |   &lt;----------call(start play)   |
         |   result(playing)---------->    |
         |   &lt;-------------data(playing)   |
         |   &lt;-----------call(stop play)   |
         |   result(stopped)---------->    |
         |                                 |
         |   &lt;-------call(start publish)   |
         |   result(publishing)------->    |
         |   data(publishing)--------->    |
         |   &lt;--------call(stop publish)   |
         |   result(stopped)(1)------->    |
         |                                 |
         |   &lt;--------------------report   |
         |   final(2)----------------->    |
         |           &lt;END>                 |
         
@See: class SrsBandwidth comments.
</pre>

Beijing, 2013.10<br/>
Winlin


