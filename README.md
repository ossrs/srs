#Simple-RTMP-Server

SRS定位是运营级的互联网直播服务器集群，追求更好的概念完整性和最简单实现的代码。

下载发布版(github): 
[Centos6-x86_64](http://winlinvip.github.io/srs.release/releases/files/SRS-CentOS6-x86_64-0.9.134.zip) 
其他[more...](http://winlinvip.github.io/srs.release/releases/) <br/>
下载发布版(国内阿里云镜像): 
[Centos6-x86_64](http://www.ossrs.net:8085/srs/releases/files/SRS-CentOS6-x86_64-0.9.134.zip) 
其他[more...](http://www.ossrs.net:8085/srs/releases/)<br/>
加入QQ群: [http://url.cn/WAHICw](http://url.cn/WAHICw) (Group: 212189142)<br/>
同类产品：[BLS](https://github.com/wenjiegit/Bull-Live-Server)/[BLE](https://github.com/wenjiegit/Bull-Live-Encoder), [NGINX-RTMP](https://github.com/arut/nginx-rtmp-module), [CRTMPD](http://www.rtmpd.com/), [RED5](http://www.red5.org/), [WOWZA](http://www.wowza.com/), [FMS/AMS](http://www.adobe.com/products/adobe-media-server-standard.html)

获得源码(github): [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server) [GIT使用方法](https://github.com/winlinvip/simple-rtmp-server/wiki/Git)

```bash
git clone https://github.com/winlinvip/simple-rtmp-server.git
```

获得源码(国内CSDN镜像): [https://code.csdn.net/winlinvip/srs-csdn](https://code.csdn.net/winlinvip/srs-csdn) [GIT使用方法](https://github.com/winlinvip/simple-rtmp-server/wiki/Git)

```bash
git clone https://code.csdn.net/winlinvip/srs-csdn.git
```

报告问题(BugReport): [https://github.com/winlinvip/simple-rtmp-server/issues/new](https://github.com/winlinvip/simple-rtmp-server/issues/new)<br/>
中文资料(Wiki): [https://github.com/winlinvip/simple-rtmp-server/wiki](https://github.com/winlinvip/simple-rtmp-server/wiki) <br/>
使用步骤(Usage): [https://github.com/winlinvip/simple-rtmp-server#usage](#usage) <br/>
公用机器(LiveShow): [https://github.com/winlinvip/simple-rtmp-server/wiki/LiveShow](https://github.com/winlinvip/simple-rtmp-server/wiki/LiveShow) <br/>
捐款(Donation): [GitHub](http://winlinvip.github.io/srs.release/donation/index.html) 
或 [阿里云镜像](http://www.ossrs.net:8085/srs/donation/index.html) ，查看
[捐献墙(Donations)](https://github.com/winlinvip/simple-rtmp-server/blob/master/DONATIONS.txt)<br/>

## About

SRS(SIMPLE RTMP Server) over state-threads created in 2013.10.

SRS is a simple, [RTMP](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryRTMP)/[HLS](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS), 
[high-performance](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance), single/multiple(plan) processes, edge/origin live server, 
[x86/x64/arm](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLinuxArm), 
compile depends on [st](http://sourceforge.net/projects/state-threads)(required), [ssl](http://www.openssl.org/) and [http-parser](https://github.com/joyent/http-parser), 
use [nginx](http://nginx.org/), [ffmpeg](http://ffmpeg.org/) and [cherrypy](http://www.cherrypy.org/) as external tools. that is, only need st to run srs for 
minimum run. see [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/Build).

SRS supports [vhost](https://github.com/winlinvip/simple-rtmp-server/wiki/RtmpUrlVhost), 
rtmp([encoder push](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryRTMP), client/[edge](https://github.com/winlinvip/simple-rtmp-server/wiki/Edge) pull), [ingester(srs pull)](https://github.com/winlinvip/simple-rtmp-server/wiki/Ingest), 
[HLS](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS), [HLS audio only](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS#hlsaudioonly), [transcoding](https://github.com/winlinvip/simple-rtmp-server/wiki/FFMPEG), 
[forward](https://github.com/winlinvip/simple-rtmp-server/wiki/FFMPEG), [http hooks](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback), [http api](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPApi), 
[http server](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPServer), [dvr](https://github.com/winlinvip/simple-rtmp-server/wiki/DVR), [FlashP2P](https://github.com/winlinvip/simple-rtmp-server/wiki/FlashP2P).

注意：FlashP2P系统为[chnvideo.com](http://www.chnvideo.com)商业方案，SRS只是支持对接。

Release: [http://winlinvip.github.io/srs.release](http://winlinvip.github.io/srs.release)  <br/>
Blog: [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin)  <br/>
CSDN mirror: [https://code.csdn.net/winlinvip/srs-csdn](https://code.csdn.net/winlinvip/srs-csdn) <br/>
See also: [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server)  <br/>
Github DEMO: [demo with your SRS](http://winlinvip.github.io/srs.release/trunk/research/players/srs_player.html?server=192.168.1.170&vhost=192.168.1.170)  <br/>
Wiki: [https://github.com/winlinvip/simple-rtmp-server/wiki](https://github.com/winlinvip/simple-rtmp-server/wiki)  <br/>
TencentQQ: [http://url.cn/WAHICw](http://url.cn/WAHICw) (Group: 212189142) <br/>
StreamServers：[BLS](https://github.com/wenjiegit/Bull-Live-Server)/[BLE](https://github.com/wenjiegit/Bull-Live-Encoder), [NGINX-RTMP](https://github.com/arut/nginx-rtmp-module), [CRTMPD](http://www.rtmpd.com/), [RED5](http://www.red5.org/), [WOWZA](http://www.wowza.com/), [FMS/AMS](http://www.adobe.com/products/adobe-media-server-standard.html)

## AUTHORS
The PRIMARY AUTHORS are (and/or have been)(Authors ordered by first contribution): 
* winlin([winterserver](#)): [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) 
* wenjie([wenjiegit](https://github.com/wenjiegit/simple-rtmp-server)): [http://blog.chinaunix.net/uid/25006789.html](http://blog.chinaunix.net/uid/25006789.html) 

About the primary AUTHORS: 
* Contribute important features to SRS. 
* Names of all PRIMARY AUTHORS response in NetConnection.connect and metadata. 
* Names of all CONTRIBUTORS response in api/v1/authors.

And here is an inevitably incomplete list of MUCH-APPRECIATED CONTRIBUTORS --
people who have submitted patches, reported bugs, added translations, helped
answer newbie questions, and generally made SRS that much better: [AUTHORS.txt](https://github.com/winlinvip/simple-rtmp-server/blob/master/AUTHORS.txt)

A big THANK YOU goes to:
* [chnvideo](chnvideo.com) co-founders([wiseyoung](mailto:wiseyoung@chnvideo.com), [trueice](mailto:trueice@chnvideo.com), [leijian](mailto:leijian@chnvideo.com)) for [big supports](https://github.com/winlinvip/simple-rtmp-server/wiki/Product#bigthanks).
* Genes amd Mabbott for creating [st](https://github.com/winlinvip/state-threads)([state-threads](http://sourceforge.net/projects/state-threads/)).
* Michael Talyanksy for introducing us to use st.
* Roman Arutyunyan for creating [nginx-rtmp](https://github.com/arut/nginx-rtmp-module) for SRS to refer to. 
* Joyent for creating [http-parser](https://github.com/joyent/http-parser) for http-api for SRS.
* Igor Sysoev for creating [nginx](http://nginx.org/) for SRS to refer to.
* [FFMPEG](http://ffmpeg.org/) and [libx264](http://www.videolan.org/) group for SRS to use to transcode.
* Guido van Rossum for creating Python for api-server for SRS.

## Usage

<strong>Step 1:</strong> get SRS 

<pre>
git clone https://github.com/winlinvip/simple-rtmp-server &&
cd simple-rtmp-server/trunk
</pre>

<strong>Step 2:</strong> build SRS,
<strong>Requires Centos6.x/Ubuntu12 32/64bits, others see [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/Build)</strong>

<pre>
./configure && make
</pre>

<strong>Step 3:</strong> start SRS 

<pre>
./objs/srs -c conf/srs.conf
</pre>

<strong>See also:</strong>
* [Usage: How to delivery RTMP?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleRTMP)
* [Usage: How to delivery HLS?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleHLS)
* [Usage: How to delivery HLS for other codec?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleTranscode2HLS)
* [Usage: How to transode RTMP stream by SRS?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleFFMPEG)
* [Usage: How to forward stream to other server?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleForward)
* [Usage: How to deploy low lantency application?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleRealtime)
* [Usage: How to deploy SRS on ARM?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleARM)
* [Usage: How to ingest file/stream/device to SRS?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleIngest)
* [Usage: How to use SRS-HTTP-server to delivery HTTP/HLS stream?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleHTTP)
* [Usage: How to show the demo of SRS?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleDemo)
* [Usage: Solution using SRS?](https://github.com/winlinvip/simple-rtmp-server/wiki/Sample)
* [Usage: Why SRS?](https://github.com/winlinvip/simple-rtmp-server/wiki/Product)

## Wiki

Please select your language:
* [English](https://github.com/winlinvip/simple-rtmp-server/wiki/ENHome)
* [Chinese](https://github.com/winlinvip/simple-rtmp-server/wiki/CNHome)

## Donation

[http://winlinvip.github.io/srs.release/donation/index.html](http://winlinvip.github.io/srs.release/donation/index.html) 
OR [aliyun mirror](http://www.ossrs.net:8085/srs/donation/index.html)

Donations:<br/>
[https://github.com/winlinvip/simple-rtmp-server/blob/master/DONATIONS.txt](https://github.com/winlinvip/simple-rtmp-server/blob/master/DONATIONS.txt)

## System Requirements
Supported operating systems and hardware:
* All Linux , both 32 and 64 bits
* All hardware.

## Summary
1. Simple: also stable enough.
1. [High-performance](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance): single-thread, async socket, event/st-thread driven.
1. Support RTMP [edge](https://github.com/winlinvip/simple-rtmp-server/wiki/Edge) server, support origin server.
1. RTMP without vod streaming, live streaming only.
1. Support multiple processes, support single process.
1. Support vhost, support \_\_defaultVhost\_\_.
1. Support adobe rtmp live streaming.
1. Support apple [HLS(m3u8)](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS) live streaming.
1. Support HLS audio-only live streaming.
1. Support reload config to enable changes.
1. Support cache last gop for flash player to fast startup.
1. Support listen at multiple ports.
1. Support long time(>4.6hours) publish/play.
1. High performace, 1800 connections(500kbps), 900Mbps, CPU 90.2%, 41MB
1. Support forward publish stream to build active-standby [cluster](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster).
1. Support broadcast by forward the stream to other servers(origin/edge).
1. Support live stream transcoding by ffmpeg.
1. Support live stream forward(acopy/vcopy) by ffmpeg.
1. Support ffmpeg filters(logo/overlay/crop), x264 params, copy/vn/an.
1. Support audio transcode only, speex/mp3 to aac
1. Support [http callback api hooks](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback)(for authentication and injection).
1. Support bandwidth test api and flash client.
1. Player, publisher(encoder), and demo pages(jquery+bootstrap). 
1. Demo video meeting or chat(SRS+cherrypy+jquery+bootstrap). 
1. Full documents in wiki, in chineses. 
1. Support RTMP(play-publish) library: srs-librtmp
1. Support ARM([debian armhf, v7cpu](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLinuxArm)) with rtmp/ssl/hls/librtmp.
1. Support [init.d](https://github.com/winlinvip/simple-rtmp-server/wiki/LinuxService) and packge script, log to file. 
1. Support RTMP ATC for HLS/HDS to support backup(failover)
1. Support http RESTful management api.
1. Support embeded http server for hls(live/vod)
1. Support stream ingester using ffmpeg.
1. Support ingest RTSP(RTP, SDP) stream to RTMP.
1. Support dvr(record live to flv file for vod)
1. Support live flashP2P(integrated by chnvideo VDN).
1. Support RTMP [edge](https://github.com/winlinvip/simple-rtmp-server/wiki/Edge) server, push/pull stream from any RTMP server
1. Support tracable log, session based log.
1. Support [vod stream(http flv/hls vod stream)](https://github.com/winlinvip/simple-rtmp-server/wiki/FlvVodStream).
1. Support DRM [token traverse](https://github.com/winlinvip/simple-rtmp-server/wiki/DRM#tokentraverse) for fms origin authenticate.
1. [dev] Support system full utest on gtest.
1. [plan] Support RTMP 302 redirect [#92](https://github.com/winlinvip/simple-rtmp-server/issues/92).
1. [plan] Support multiple processes, for both origin and edge
1. [no-plan] Support adobe RTMFP(flash p2p) protocol.
1. [no-plan] Support adobe flash refer/token/swf verification.
1. [no-plan] Support adobe amf3 codec.
1. [no-plan] Support encryption: RTMPE/RTMPS, HLS DRM
1. [no-plan] Support RTMPT, http to tranverse firewalls
1. [no-plan] Support file source, transcoding file to live stream
1. [no-plan] Support RTP/RTSP server.

## Releases
* 2014-06-27, [Release v1.0-mainline5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline5), refine perf 3k+ clients, edge token traverse, [srs monitor](http://ossrs.net:1977), 30days online. 41573 lines.<br/>
* 2014-05-28, [Release v1.0-mainline4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline4), support heartbeat, tracable log, fix mem leak and bugs. 39200 lines.<br/>
* 2014-05-18, [Release v1.0-mainline3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline3), support mips, fms origin, json(http-api). 37594 lines.<br/>
* 2014-04-28, [Release v1.0-mainline2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline2), support [dvr](https://github.com/winlinvip/simple-rtmp-server/wiki/DVR), android, [edge](https://github.com/winlinvip/simple-rtmp-server/wiki/Edge). 35255 lines.<br/>
* 2014-04-07, [Release v1.0-mainline](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline), support [arm](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLinuxArm), [init.d](https://github.com/winlinvip/simple-rtmp-server/wiki/LinuxService), http [server](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPServer)/[api](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPApi), [ingest](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleIngest). 30000 lines.<br/>
* 2013-12-25, [Release v0.9](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.9), support bandwidth test, player/encoder/chat [demos](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleDemo). 20926 lines.<br/>
* 2013-12-08, [Release v0.8](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.8), support [http hooks callback](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback), update [st_load](https://github.com/winlinvip/st-load). 19186 lines.<br/>
* 2013-12-03, [Release v0.7](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.7), support [live stream transcoding](https://github.com/winlinvip/simple-rtmp-server/wiki/FFMPEG). 17605 lines.<br/>
* 2013-11-29, [Release v0.6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.6), support [forward](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster) stream to origin/edge. 16094 lines.<br/>
* 2013-11-26, [Release v0.5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.5), support [HLS(m3u8)](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS), fragment and window. 14449 lines.<br/>
* 2013-11-10, [Release v0.4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.4), support [reload](https://github.com/winlinvip/simple-rtmp-server/wiki/Reload) config, pause, longtime publish/play. 12500 lines.<br/>
* 2013-11-04, [Release v0.3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.3), support [vhost](https://github.com/winlinvip/simple-rtmp-server/wiki/RtmpUrlVhost), refer, gop cache, listen multiple ports. 11773 lines.<br/>
* 2013-10-25, [Release v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2), support [rtmp](https://github.com/winlinvip/simple-rtmp-server/wiki/RTMPHandshake) flash publish, h264, time jitter correct. 10125 lines.<br/>
* 2013-10-23, [Release v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1), support [rtmp FMLE/FFMPEG publish](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryRTMP), vp6. 8287 lines.<br/>
* 2013-10-17, Created.<br/>

## History
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
* v1.0, 2014-06-22, performance refine, support [3k+](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance#%E6%80%A7%E8%83%BD%E4%BE%8B%E8%A1%8C%E6%8A%A5%E5%91%8A4k) connections(270kbps). 0.9.130
* v1.0, 2014-06-21, support edge [token traverse](https://github.com/winlinvip/simple-rtmp-server/wiki/DRM#tokentraverse), fix [#104](https://github.com/winlinvip/simple-rtmp-server/issues/104). 0.9.129
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
* v1.0, 2014-04-24, support live flashP2P(integrated by chnvideo VDN). 0.9.75
* v1.0, 2014-04-21, support android app to start srs for internal edge. 0.9.72
* v1.0, 2014-04-19, support tool over srs-librtmp to ingest flv/rtmp. 0.9.71
* v1.0, 2014-04-17, support dvr(record live to flv file for vod). 0.9.69
* v1.0, 2014-04-11, add speex1.2 to transcode flash encoder stream. 0.9.58
* v1.0, 2014-04-10, support reload ingesters(add/remov/update). 0.9.57
* <strong>v1.0, 2014-04-07, [1.0 mainline(0.9.55)](https://github.com/winlinvip/simple-rtmp-server/releases/tag/1.0.mainline) released. 30000 lines.</strong>
* v1.0, 2014-04-07, support [ingest](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleIngest) file/stream/device.
* v1.0, 2014-04-05, support [http api](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPApi) and [http server](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPServer).
* v1.0, 2014-04-03, implements http framework and api/v1/version.
* v1.0, 2014-03-30, fix bug for st detecting epoll failed, force st to use epoll.
* v1.0, 2014-03-29, add wiki [Performance for RaspberryPi](https://github.com/winlinvip/simple-rtmp-server/wiki/RaspberryPi).
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
* v1.0, 2014-03-16, support ARM([debian armhf, v7cpu](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLinuxArm)) with rtmp/ssl/hls/librtmp.
* v1.0, 2014-03-12, finish utest for amf0 codec.
* v1.0, 2014-03-06, add gperftools for mem leak detect, mem/cpu profile.
* v1.0, 2014-03-04, add gest framework for utest, build success.
* v1.0, 2014-03-02, add wiki [srs-librtmp](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLibrtmp), [SRS for arm](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLinuxArm), [product](https://github.com/winlinvip/simple-rtmp-server/wiki/Product)
* v1.0, 2014-03-02, srs-librtmp, client publish/play library like librtmp.
* v1.0, 2014-03-01, modularity, extract core/kernel/rtmp/app/main module.
* v1.0, 2014-02-28, support arm build(SRS/ST), add ssl to 3rdparty package.
* v1.0, 2014-02-28, add wiki [BuildArm](https://github.com/winlinvip/simple-rtmp-server/wiki/Build), [FFMPEG](https://github.com/winlinvip/simple-rtmp-server/wiki/FFMPEG), [Reload](https://github.com/winlinvip/simple-rtmp-server/wiki/Reload)
* v1.0, 2014-02-27, add wiki [LowLatency](https://github.com/winlinvip/simple-rtmp-server/wiki/LowLatency), [HTTPCallback](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback), [ServerSideScript](https://github.com/winlinvip/simple-rtmp-server/wiki/ServerSideScript), [IDE](https://github.com/winlinvip/simple-rtmp-server/wiki/IDE)
* v1.0, 2014-01-19, add wiki [DeliveryHLS](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS)
* v1.0, 2014-01-12, add wiki [HowToAskQuestion](https://github.com/winlinvip/simple-rtmp-server/wiki/HowToAskQuestion), [RtmpUrlVhost](https://github.com/winlinvip/simple-rtmp-server/wiki/RtmpUrlVhost)
* v1.0, 2014-01-11, fix jw/flower player pause bug, which send closeStream actually.
* v1.0, 2014-01-05, add wiki [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/Build), [Performance](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance), [Cluster](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster)
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
* v0.8, 2013-12-08, support [http hooks](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback): on_connect/close/publish/unpublish/play/stop.
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

Performance benchmark history, on virtual box:
* 2014-07-12, SRS 0.9.156, 2700clients, 89%CPU, 61MB. [benchmark](https://github.com/winlinvip/simple-rtmp-server/commit/6d12280b7cc54c465b1caf8b1402149e77c4c7d9)
* 2014-07-12, SRS 0.9.156, 1800clients, 68%CPU, 38MB. [benchmark](https://github.com/winlinvip/simple-rtmp-server/commit/e2d273f4939348374bf9644df9d54c4293b39c1a)
* 2013-11-28, SRS 0.5.0, 1800clients, 90%CPU, 41MB. [benchmark](https://github.com/winlinvip/simple-rtmp-server/commit/023e23bc8261bec15a70a7ae932098fb4f82b679)

1.  300 connections,  150Mbps, 500kbps, CPU 5.7%, MEM 9208KB.
1.  600 connections,  300Mbps, 500kbps, CPU 18.3%, MEM 13MB.
1.  900 connections,  450Mbps, 500kbps, CPU 27.9%, MEM 20MB.
1. 1200 connections,  600Mbps, 500kbps, CPU 43.9%, MEM 26MB.
1. 1500 connections,  750Mbps, 500kbps, CPU 55.2%, MEM 32MB.
1. 1800 connections,  900Mbps, 500kbps, CPU 68.8%, MEM 38MB.
1. 2100 connections, 1050Mbps, 500kbps, CPU 75.7%, MEM 46MB.
1. 2400 connections, 1200Mbps, 500kbps, CPU 83.7%, MEM 54MB.
1. 2700 connections, 1350Mbps, 500kbps, CPU 89.9%, MEM 61MB.

<pre>
[winlin@dev6 srs]$ dstat
----total-cpu-usage---- -dsk/total- ---net/lo-- ---paging-- ---system--
usr sys idl wai hiq siq| read  writ| recv  send|  in   out | int   csw 
 29  17  39   0   0  15|   0  5325B| 163M  163M|   0     0 |4331  3386 
 30  16  38   0   0  16|   0  5325B| 160M  160M|   0     0 |4252  3332 
 30  15  37   0   0  17|   0  7646B| 169M  169M|   0     0 |4015  2886 
 30  17  36   0   0  17|   0  1638B| 197M  197M|   0     0 |4021  3037 
 31  17  35   0   0  17|   0   410B| 204M  204M|   0     0 |4181  3243 
 33  17  32   0   0  18|   0  2185B| 191M  191M|   0     0 |4305  3592 
 31  15  36   0   0  18|   0  1229B| 127M  127M|   0     0 |4446  3822 
 34  18  30   0   0  18|   0     0 | 231M  231M|   0     0 |4461  3691 
 32  17  33   0   0  18|   0   410B| 169M  169M|   0     0 |4518  3788 
</pre>

* See also: [Performance for x86/x64 Test Guide](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance)
* See also: [Performance for RaspberryPi](https://github.com/winlinvip/simple-rtmp-server/wiki/RaspberryPi)

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

### [HDS/HLS origin backup](https://github.com/winlinvip/simple-rtmp-server/wiki/RTMP-ATC)

<pre>
                        +----------+        +----------+
               +--ATC->-+  server  +--ATC->-+ packager +-+   +---------+
+----------+   | RTMP   +----------+ RTMP   +----------+ |   | Reverse |    +-------+
| encoder  +->-+                                         +->-+  Proxy  +-->-+  CDN  +
+----------+   |        +----------+        +----------+ |   | (nginx) |    +-------+
               +--ATC->-+  server  +--ATC->-+ packager +-+   +---------+
                 RTMP   +----------+ RTMP   +----------+
</pre>

### [RTMP cluster(origin/edge) Architecture](https://github.com/winlinvip/simple-rtmp-server/wiki/Edge)

Remark: cluster over forward, see [Cluster](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster)
Remark: cluster over edge, see [Edge](https://github.com/winlinvip/simple-rtmp-server/wiki/Edge)

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
</pre>

### (plan) CLI Architecture

<pre>
                       +---------+
                    +--+ stream1 +---------+
                    |  +---------+         |
 +--------+         |  +---------+         |   +-------+
 | master +--fork->-+--+ streamN +---amf0--+>--+  cli  +
 +--------+         |  +---------+         |   +-------+
                    |  +-------------+     |
                    +--+ back source +-----+
                       +-------------+
Remark:
(1) master listen the global api port, for example, 33330
(2) back source and stream processes listen at private api port, 
    for example, 33331, 33332, 33333
(3) work processes(stream and back-source), report private api
    port to master global api port.
(4) cli connect to master global api port, get all other private
    api ports
(5) cli connect to each stream/back-source process to get api data,
    cli analysis and summary the data, return to user.
</pre>

### Live FlashP2P

<pre>
                                  +--DVR------>-(flv file)
+----------+          +-----+     |          
| encoder  +--RTMP-->-+ SRS +-->--+          
+----------+          +-----+     |          
                                  |           +------------+
                                  +---HTTP-->-+ P2P system +
                                    callback  +------------+
</pre>

Remark: P2P system provides by [chnvideo.com](http://www.chnvideo.com)

注意：FlashP2P系统为[chnvideo.com](http://www.chnvideo.com)商业方案，SRS只是支持对接。

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


