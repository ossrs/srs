#Simple-RTMP-Server

[![CircleCI](https://circleci.com/gh/ossrs/srs/tree/develop.svg?style=svg)](https://circleci.com/gh/ossrs/srs/tree/develop)
[![Gitter](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/ossrs/srs?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge)
[![Wechat](https://cloud.githubusercontent.com/assets/2777660/22814959/c51cbe72-ef92-11e6-81cc-32b657b285d5.png)](https://github.com/ossrs/srs/wiki/v1_CN_Contact#wechat)

SRS/3.0, [OuXuli][release3]<br/>
SRS是大娱乐直播领域广泛应用的野鸡服务器，志存高远不限于色情行业。<br/>
SRS is an industrial-strength live cluster, with simple code and best conceptual integrity.

Download from github.io: [Centos6-x86_64][centos0], [more...][more0]<br/>
Download from ossrs.net: [Centos6-x86_64][centos1], [more...][more1]<br/>
For the wiki for SRS/3.0, please read [Chinese][srs_CN] or [English][srs_EN].

## Content

* [About](#about)
* [Product](#product)
* [Usage](#usage)
* [Wiki for v3.0](#srs-30-wiki)
* [Wiki for v2.0](#srs-20-wiki)
* [Wiki for v1.0](#srs-10-wiki)
* [Features](#features)
* [v3.0 changes](#v3-changes)
* [v2.0 changes](#v2-changes)
* [v1.0 changes](#v1-changes)
* [Releases](#releases)
* [Compare](#compare)
* [Performance](#performance)
* [Architecture](#architecture)
* [System Architecture](#system-architecture)
* [Modularity Architecture](#modularity-architecture)
* [Stream Architecture](#stream-architecture)
* [Tips](#tips)
* [Authors](#authors)
* [Mirrors](#mirrors)
* [System Requirements](#system-requirements)

## About

SRS(Simple RTMP Server) is created in 2013.10.
SRS supports many protocols includes RTMP, HLS, HTTP-FLV and HDS.
SRS can run on both LINUX and OSX, and X86, X64, ARM and MIPS cpu.
Vhost is used as service unit for live cluster, which delivery stream by origin and edge.
The stream on origin can be transcoded, DVR to VOD file, ingest from external sources, or forwarded to other servers.
HTTP API and callback is powerful mechenism for integraty.
The wikis are writen by both English and Chinese.

Why SRS?

1. <strong>Completely rewriten HLS:</strong> Following m3u8/ts spec, and HLS support h.264+aac/mp3.
1. <strong>High efficient RTMP:</strong> Deliverying support 7k+ concurrency, vhost based, both origin and edge.
1. <strong>Embeded HTTP server:</strong> For HLS, api and HTTP flv/ts/mp3/aac streaming.
1. <strong>Variety inputs:</strong> RTMP, pull by ingest file or stream(HTTP/RTMP/RTSP), push by stream caster 
RTSP/MPEGTS-over-UDP.
1. <strong>Popular internet delivery:</strong> RTMP/HDS for flash, HLS for mobile(IOS/IPad/MAC/Android), HTTP 
flv/ts/mp3/aac streaming for user prefered.
1. <strong>Enhanced DVR:</strong> Segment/session/append plan, customer path and HTTP callback, to FLV/MP4 file.
1. <strong>Multiple features:</strong> Transcode, forward, ingest, http hooks, dvr, hls, rtsp, http streaming, 
http raw api, refer, log, bandwith test and srs-librtmp.
1. <strong>Best maintainess:</strong> Simple arch over state-threads(coroutine), single thread, single process 
and for linux/osx platform, common server x86-64/i386/arm/mips cpus, rich comments, strictly 
follows RTMP/HLS/RTSP spec.
1. <strong>Easy to use:</strong> Both English and Chinese wiki, typically config files in trunk/conf, traceable 
and session based log, linux service script and install script.
1. <strong>MIT license:</strong> Open source with product management and evolution.

Enjoy it!

## Product

The `usage`, `wiki`, `changes`, `features`, `compare`, `release` and `performance` of SRS.

### Usage

<strong>Step 1:</strong> Get SRS.
<strong>Download slow? Please use [mirrors](#mirrors) for SRS.</strong>

```
git clone https://github.com/ossrs/srs &&
cd srs/trunk
```

<strong>Step 2:</strong> Build SRS.
<strong>Requires Centos6 32/64bits, others see Build([CN][v3_CN_Build],[EN][v3_EN_Build]).</strong>

```
./configure && make
```

<strong>Step 3:</strong> Start SRS 

```
./objs/srs -c conf/srs.conf
```

<strong>See also:</strong>
* Usage: How to delivery RTMP?([CN][v1_CN_SampleRTMP], [EN][v1_EN_SampleRTMP])
* Usage: How to delivery RTMP Cluster?([CN][v3_CN_SampleRTMPCluster], [EN][v3_EN_SampleRTMPCluster])
* Usage: How to delivery HTTP FLV Live Streaming?([CN][v2_CN_SampleHttpFlv], [EN][v2_EN_SampleHttpFlv])
* Usage: How to delivery HTTP FLV Live Streaming Cluster?([CN][v3_CN_SampleHttpFlvCluster], [EN][v3_EN_SampleHttpFlvCluster])
* Usage: How to delivery HLS?([CN][v2_CN_SampleHLS], [EN][v2_EN_SampleHLS])
* Usage: How to delivery HLS for other codec?([CN][v2_CN_SampleTranscode2HLS], [EN][v2_EN_SampleTranscode2HLS])
* Usage: How to transode RTMP stream by SRS?([CN][v2_CN_SampleFFMPEG], [EN][v2_EN_SampleFFMPEG])
* Usage: How to forward stream to other server?([CN][v3_CN_SampleForward], [EN][v3_EN_SampleForward])
* Usage: How to deploy low lantency application?([CN][v3_CN_SampleRealtime], [EN][v3_EN_SampleRealtime])
* Usage: How to deploy SRS on ARM?([CN][v1_CN_SampleARM], [EN][v1_EN_SampleARM])
* Usage: How to ingest file/stream/device to SRS?([CN][v1_CN_SampleIngest], [EN][v1_EN_SampleIngest])
* Usage: How to use SRS-HTTP-server to delivery HTTP/HLS stream?([CN][v2_CN_SampleHTTP], [EN][v2_EN_SampleHTTP])
* Usage: How to show the demo of SRS? ([CN][v1_CN_SampleDemo], [EN][v1_EN_SampleDemo])
* Usage: How to publish h.264 raw stream to SRS? ([CN][v3_CN_SrsLibrtmp2], [EN][v3_EN_SrsLibrtmp2])
* Usage: Solution using SRS?([CN][v1_CN_Sample], [EN][v1_EN_Sample])
* Usage: Why SRS?([CN][v1_CN_Product], [EN][v1_EN_Product])

### SRS 1.0 wiki

Please select your language:
* [SRS 1.0 English][v1-wiki-en]
* [SRS 1.0 Chinese][v1-wiki-cn]

### SRS 2.0 wiki

Please select your language:
* [SRS 2.0 English][v2-wiki-en]
* [SRS 2.0 Chinese][v2-wiki-cn]

### SRS 3.0 wiki

Please select your language:
* [SRS 3.0 English][v3_EN_Home]
* [SRS 3.0 Chinese][v3_CN_Home]

### Features

- [x] Simple and stable enough.
- [x] Support RTMP origin-edge cluster, for origin ([CN][v1_CN_DeliveryRTMP],[EN][v1_EN_DeliveryRTMP]), for edge([CN][v3_CN_Edge], [EN][v3_EN_Edge]).
- [x] Support remux RTMP to HTTP-FLV([CN][v2_CN_SampleHttpFlv], [EN][v2_EN_SampleHttpFlv]) or HLS([CN][v2_CN_DeliveryHLS], [EN][v2_EN_DeliveryHLS]).
- [x] High-performance([CN][v1_CN_Performance], [EN][v1_EN_Performance]): single-thread, async socket, event(st) driven.
- [x] High-concurrency([CN][v1_CN_Performance], [EN][v1_EN_Performance]), 6000+ connections(500kbps), 900Mbps, CPU 90.2%, 41MB.
- [x] Support Vhost([CN][v1_CN_RtmpUrlVhost], [EN][v1_EN_RtmpUrlVhost]) and \_\_defaultVhost\_\_.
- [x] Support HLS audio-only([CN][v2_CN_DeliveryHLS2], [EN][v2_EN_DeliveryHLS2]) live streaming.
- [x] Support reload([CN][v1_CN_Reload], [EN][v1_EN_Reload]) config to hot apply config.
- [x] Support gop-cache to cache last gop([CN][v3_CN_LowLatency2], [EN][v3_EN_LowLatency2]) for fast startup.
- [x] Support listen at multiple ports.
- [x] Support RTMP long time(>4.6hours) publish/play.
- [x] Support forward([CN][v3_CN_Forward], [EN][v3_EN_Forward]) in master-slave mode.
- [x] Support transcoding([CN][v3_CN_FFMPEG], [EN][v3_EN_FFMPEG]) by fork ffmpeg.
- [x] Support HTTP-callback([CN][v3_CN_HTTPCallback], [EN][v3_EN_HTTPCallback]) for authentication and injection.
- [x] Support bandwidth test([CN][v1_CN_BandwidthTestTool], [EN][v1_EN_BandwidthTestTool]) api and flash client.
- [x] Support wiki in both [Chinese][v3_CN_Home] and [English][v3_EN_Home]. 
- [x] Support RTMP client library: srs-librtmp([CN][v3_CN_SrsLibrtmp], [EN][v3_EN_SrsLibrtmp])
- [x] Support ARM([CN][v1_CN_SrsLinuxArm], [EN][v1_EN_SrsLinuxArm]) with rtmp/ssl/hls/librtmp.
- [x] Support HTTP-API([CN][v3_CN_HTTPApi], [EN][v3_EN_HTTPApi]) for management.
- [x] Support ingest([CN][v1_CN_Ingest], [EN][v1_EN_Ingest]) other protocol to RTMP by ffmpeg.
- [x] Support DVR([CN][v3_CN_DVR], [EN][v3_EN_DVR]) to record live to flv file.
- [x] Support tracable log, session based log([CN][v1_CN_SrsLog], [EN][v1_EN_SrsLog]).
- [x] Support Adobe FMS/AMS token traverse([CN][v3_CN_DRM2], [EN][v3_EN_DRM2]).
- [x] Support embeded HTTP server([CN][v2_CN_SampleHTTP], [EN][v2_EN_SampleHTTP]) for http streaming.
- [x] Support publish h264 raw stream([CN][v3_CN_SrsLibrtmp2], [EN][v3_EN_SrsLibrtmp2]) by srs-librtmp.
- [x] Support publish aac adts raw stream([CN][v3_CN_SrsLibrtmp3], [EN][v3_EN_SrsLibrtmp3]) by srs-librtmp.
- [x] Support low latency(0.1s+), please read [bug #257][bug #257].
- [x] Support security to allow/deny publish/play ip([CN][v2_CN_Security], [EN][v2_EN_Security]).
- [x] Support remux RTMP to http flv/mp3/aac/ts live stream, please read wiki([CN][v2_CN_DeliveryHttpStream], [EN][v2_CN_DeliveryHttpStream]).
- [x] Support HLS with mp3(h.264+mp3), please read [bug #301][bug #301].
- [x] Support EXEC introduced by nginx-rtmp, please read [bug #367][bug #367].
- [x] Support DVR control module introduced by nginx-rtmp, please read [#459][bug #459].
- [x] Support HTTP RAW API, please read [#459][bug #459], [#470][bug #470], [#319][bug #319].
- [x] Support http api/stream CORS for js.
- [x] Support valgrind and latest ARM by patch ST.
- [x] [experiment] Support big-data with Kafka, please read [#467][bug #467].
- [x] [experiment] Support Adobe HDS(f4m), please read wiki([CN][v2_CN_DeliveryHDS], [EN][v2_EN_DeliveryHDS]).
- [x] [experiment] Support push MPEG-TS over UDP to SRS, please read [bug #250][bug #250].
- [x] [experiment] Support push RTSP to SRS, please read [bug #133][bug #133].
- [x] [experiment] Support push POST FLV over HTTP, please read [wiki]([CN][v2_CN_Streamer2], [EN][v2_EN_Streamer2]).
- [x] [experiment] Support multiple processes by [dolphin][srs-dolphin] or [oryx][oryx].
- [x] [experiment] Support [mgmt console][console], please read [srs-ngb][srs-ngb].
- [ ] Enhanced HLS audio-only use aac instead of ts.
- [ ] Enhanced forward with vhost and url variables.
- [ ] Support source or idle stream cleanup.
- [ ] Support origin cluster, please read [#464][bug #464], [RTMP 302][bug #92].
- [ ] Support H.265, push RTMP with H.265, delivery in HLS, read [#465][bug #465].
- [ ] Support MPEG-DASH, the future streaming protocol, read [#299][bug #299].
- [ ] Support HLS+, please read [#466][bug #466] and [#468][bug #468].

### Change Logs

### V3 changes

* v3.0, 2017-02-07, fix [#738][bug #738] support DVR general mp4. 3.0.17
* v3.0, 2017-01-19, for [#742][bug #742] refine source, meta and origin hub. 3.0.16
* v3.0, 2017-01-17, for [#742][bug #742] refine source, timeout, live cycle. 3.0.15
* v3.0, 2017-01-11, fix [#735][bug #735] config transform refer_publish invalid. 3.0.14
* v3.0, 2017-01-06, for [#730][bug #730] support config in/out ack size. 3.0.13
* v3.0, 2017-01-06, for [#711][bug #711] support perfile for transcode. 3.0.12
* v3.0, 2017-01-05, patch ST for valgrind and ARM. 3.0.11
* v3.0, 2017-01-05, for [#324][bug #324], always enable hstrs. 3.0.10
* v3.0, 2016-12-15, fix [#717][bug #717], [#691][bug #691], http api/static/stream support cors. 3.0.9
* v3.0, 2016-12-08, support log rotate signal SIGUSR1. 3.0.8
* v3.0, 2016-12-07, fix typo and refine grammar. 3.0.7
* v3.0, 2015-10-23, fix [#467][bug #467], support write log to kafka. 3.0.6
* v3.0, 2015-10-20, fix [#502][bug #502], support snapshot with http-callback or transcoder. 3.0.5
* v3.0, 2015-09-19, support amf0 and json to convert with each other.
* v3.0, 2015-09-19, json objects support dumps to string.
* v3.0, 2015-09-14, fix [#459][bug #459], support dvr raw api. 3.0.4
* v3.0, 2015-09-14, fix [#459][bug #459], dvr support apply filter for ng-control dvr module.
* v3.0, 2015-09-14, fix [#319][bug #319], http raw api support update global and vhost. 3.0.3
* v3.0, 2015-08-31, fix [#319][bug #319], http raw api support query global and vhost.
* v3.0, 2015-08-28, fix [#471][bug #471], api response the width and height. 3.0.2
* v3.0, 2015-08-25, fix [#367][bug #367], support nginx-rtmp exec. 3.0.1

### V2 changes

* v2.0, 2017-02-09, fix [#503][bug #503] disable utilities when reload a source. 2.0.233
* v2.0, 2017-01-22, for [#752][bug #752] release the io then free it for kbps. 2.0.232
* v2.0, 2017-01-18, fix [#750][bug #750] use specific error code for dns resolve. 2.0.231
* <strong>v2.0, 2017-01-18, [2.0 beta4(2.0.230)][r2.0b4] released. 86334 lines.</strong>
* v2.0, 2017-01-18, fix [#749][bug #749], timestamp overflow for ATC. 2.0.230
* v2.0, 2017-01-11, fix [#740][bug #740], convert ts aac audio private stream 1 to common. 2.0.229
* v2.0, 2017-01-11, fix [#588][bug #588], kbps interface error. 2.0.228
* v2.0, 2017-01-11, fix [#736][bug #736], recovery the hls dispose. 2.0.227
* v2.0, 2017-01-10, refine hls html5 video template.
* v2.0, 2017-01-10, fix [#635][bug #635], hls support NonIDR(open gop). 2.0.226
* v2.0, 2017-01-06, for [#730][bug #730], reset ack follow flash player rules. 2.0.225
* v2.0, 2016-12-15, for [#513][bug #513], remove hls ram from srs2 to srs3+. 2.0.224
* <strong>v2.0, 2016-12-13, [2.0 beta3(2.0.223)][r2.0b3] released. 86685 lines.</strong>
* v2.0, 2016-12-13, fix [#713][bug #713], disable the source cleanup. 2.0.223
* v2.0, 2016-12-13, fix [#713][bug #713], refine source to avoid critical fetch and create. 2.0.222
* <strong>v2.0, 2016-11-09, [2.0 beta2(2.0.221)][r2.0b2] released. 86691 lines.</strong>
* v2.0, 2016-11-05, fix [#654][bug #654], crash when source cleanup for edge. 2.0.221
* v2.0, 2016-10-26, fix [#666][bug #666], crash when source cleanup for http-flv. 2.0.220
* v2.0, 2016-10-10, fix [#661][bug #661], close fd after thread stopped. 2.0.219
* v2.0, 2016-09-23, support asprocess for oryx. 2.0.218
* v2.0, 2016-09-23, support change work_dir for oryx.
* v2.0, 2016-09-15, fix [#640][bug #640], typo for rtmp type. 2.0.217
* v2.0, 2016-09-12, fix fast stream error bug. 2.0.216
* <strong>v2.0, 2016-09-09, [2.0 beta1(2.0.215)][r2.0b1] released. 89941 lines.</strong>
* v2.0, 2016-09-09, refine librtmp comments about NALUs. 2.0.215
* v2.0, 2016-09-05, fix memory leak at source. 2.0.214
* v2.0, 2016-09-05, fix memory leak at handshake. 2.0.213
* v2.0, 2016-09-04, support valgrind for [patched st](https://github.com/ossrs/state-threads/issues/2).
* v2.0, 2016-09-03, support all arm for [patched st](https://github.com/ossrs/state-threads/issues/1). 2.0.212
* v2.0, 2016-09-01, workaround [#511][bug #511] the fly stfd in close. 2.0.211
* v2.0, 2016-08-30, comment the pcr.
* v2.0, 2016-08-18, fix [srs-librtmp#4](https://github.com/ossrs/srs-librtmp/issues/4) filter frame.
* v2.0, 2016-08-10, fix socket timeout for librtmp.
* v2.0, 2016-08-08, fix the crash by srs_info log.
* <strong>v2.0, 2016-08-06, [2.0 beta0(2.0.210)][r2.0b0] released. 89704 lines.</strong>
* v2.0, 2016-05-17, fix the sps pps parse bug.
* v2.0, 2016-01-13, fix http reader bug, support infinite chunked. 2.0.209
* v2.0, 2016-01-09, merge [#559][pr #559] fix memory leak bug. 2.0.208
* v2.0, 2016-01-09, merge [#558][pr #558] add tcUrl for on_publish.
* v2.0, 2016-01-05, add keyword XCORE for coredump to identify the version. 2.0.207
* <strong>v2.0, 2015-12-23, [2.0 alpha3(2.0.205)][r2.0a3] released. 89544 lines.</strong>
* v2.0, 2015-12-22, for [#509][bug #509] always alloc big object at heap. 2.0.205
* v2.0, 2015-12-22, for [#418][bug #418] ignore null connect props to make RED5 happy. 2.0.204
* v2.0, 2015-12-22, for [#546][bug #546] thread terminate normally dispose bug. 2.0.203
* v2.0, 2015-12-22, for [#541][bug #541] failed when chunk size too small. 2.0.202
* v2.0, 2015-12-15, default hls_on_error to continue. 2.0.201
* v2.0, 2015-11-16, for [#518][bug #518] fix fd leak bug when fork. 2.0.200
* v2.0, 2015-11-05, for [#511][bug #511] fix bug for restart thread. 2.0.199
* v2.0, 2015-11-02, for [#515][bug #515] use srs_freepa and SrsAutoFreeA for array. 2.0.198
* v2.0, 2015-10-28, for [ExoPlayer #828][exo #828], remove duration for live.
* v2.0, 2015-10-28, for [ExoPlayer #828][exo #828], add av tag in flv header. 2.0.197
* v2.0, 2015-10-27, for [#512][bug #512] partial hotfix the hls pure audio. 2.0.196
* <strong>v2.0, 2015-10-08, [2.0 alpha2(2.0.195)][r2.0a2] released. 89358 lines.</strong>
* v2.0, 2015-10-04, for [#448][bug #448] fix the bug of response of http hooks. 2.0.195
* v2.0, 2015-10-01, for [#497][bug #497] response error when client not found to kickoff. 2.0.194
* v2.0, 2015-10-01, for [#495][bug #495] decrease the srs-librtmp size. 2.0.193
* v2.0, 2015-09-23, for [#485][bug #485] error when arm glibc 2.15+ or not i386/x86_64/amd64. 2.0.192
* v2.0, 2015-09-23, for [#485][bug #485] srs for respberrypi and cubieboard. 2.0.191
* v2.0, 2015-09-21, fix [#484][bug #484] hotfix the openssl build script 2.0.190
* <strong>v2.0, 2015-09-14, [2.0 alpha1(2.0.189)][r2.0a1] released. 89269 lines.</strong>
* v2.0, 2015-09-14, fix [#474][bug #474] config to donot parse width/height from sps. 2.0.189
* v2.0, 2015-09-14, for [#474][bug #474] always release publish for source.
* v2.0, 2015-09-14, for [#458][bug #458] http hooks use source thread cid. 2.0.188
* v2.0, 2015-09-14, for [#475][bug #475] fix http hooks crash for st context switch. 2.0.187
* v2.0, 2015-09-09, support reload utc_time. 2.0.186
* <strong>v2.0, 2015-08-23, [2.0 alpha0(2.0.185)][r2.0a0] released. 89022 lines.</strong>
* v2.0, 2015-08-22, HTTP API support JSONP by specifies the query string callback=xxx.
* v2.0, 2015-08-20, fix [#380][bug #380], srs-librtmp send sequence header when sps or pps changed.
* v2.0, 2015-08-18, close [#454][bug #454], support obs restart publish. 2.0.184
* v2.0, 2015-08-14, use reduce_sequence_header for stream control.
* v2.0, 2015-08-14, use send_min_interval for stream control. 2.0.183
* v2.0, 2015-08-12, enable the SRS_PERF_TCP_NODELAY and add config tcp_nodelay. 2.0.182
* v2.0, 2015-08-11, for [#442][bug #442] support kickoff connected client. 2.0.181
* v2.0, 2015-07-21, for [#169][bug #169] support default values for transcode. 2.0.180
* v2.0, 2015-07-21, fix [#435][bug #435] add pageUrl for HTTP callback on_play.
* v2.0, 2015-07-20, refine the hls, ignore packet when no sequence header. 2.0.179
* v2.0, 2015-07-16, for [#441][bug #441] use 30s timeout for first msg. 2.0.178
* v2.0, 2015-07-14, refine hls disable the time jitter, support not mix monotonically increase. 2.0.177
* v2.0, 2015-07-01, fix [#433][bug #433] fix the sps parse bug. 2.0.176
* v2.0, 2015-06-10, fix [#425][bug #425] refine the time jitter, correct (-inf,-250)+(250,+inf) to 10ms. 2.0.175
* v2.0, 2015-06-10, fix [#424][bug #424] fix aggregate timestamp bug. 2.0.174
* v2.0, 2015-06-06, fix [#421][bug #421] drop video for unkown RTMP header.
* v2.0, 2015-06-05, fix [#420][bug #420] remove ts for hls ram mode.
* v2.0, 2015-05-30, fix [#209][bug #209] cleanup hls when stop and timeout. 2.0.173.
* v2.0, 2015-05-29, fix [#409][bug #409] support pure video hls. 2.0.172.
* v2.0, 2015-05-28, support [srs-dolphin][srs-dolphin], the multiple-process SRS.
* v2.0, 2015-05-24, fix [#404][bug #404] register handler then start http thread. 2.0.167.
* v2.0, 2015-05-23, refine the thread, protocol, kbps code. 2.0.166
* v2.0, 2015-05-23, fix [#391][bug #391] copy request for async call.
* v2.0, 2015-05-22, fix [#397][bug #397] the USER_HZ maybe not 100. 2.0.165
* v2.0, 2015-05-22, for [#400][bug #400], parse when got entire http header, by feilong. 2.0.164.
* v2.0, 2015-05-19, merge from bravo system, add the rtmfp to bms(commercial srs). 2.0.163.
* v2.0, 2015-05-10, support push flv stream over HTTP POST to SRS.
* v2.0, 2015-04-20, support ingest hls live stream to RTMP.
* v2.0, 2015-04-15, for [#383][bug #383], support mix_correct algorithm. 2.0.161.
* v2.0, 2015-04-13, for [#381][bug #381], support reap hls/ts by gop or not. 2.0.160.
* v2.0, 2015-04-10, enhanced on_hls_notify, support HTTP GET when reap ts.
* v2.0, 2015-04-10, refine the hls deviation for floor algorithm.
* v2.0, 2015-04-08, for [#375][bug #375], fix hls bug, keep cc continous between ts files. 2.0.159.
* v2.0, 2015-04-04, for [#304][bug #304], rewrite annexb mux for ts, refer to apple sample. 2.0.157.
* v2.0, 2015-04-03, enhanced avc decode, parse the sps get width+height. 2.0.156.
* v2.0, 2015-04-03, for [#372][bug #372], support transform vhost of edge 2.0.155.
* v2.0, 2015-03-30, for [#366][bug #366], config hls to disable cleanup of ts. 2.0.154.
* v2.0, 2015-03-31, support server cycle handler. 2.0.153.
* v2.0, 2015-03-31, support on_hls for http hooks. 2.0.152.
* v2.0, 2015-03-31, enhanced hls, support deviation for duration. 2.0.151.
* v2.0, 2015-03-30, for [#351][bug #351], support config the m3u8/ts path for hls. 2.0.149.
* v2.0, 2015-03-17, for [#155][bug #155], osx(darwin) support demo with nginx and ffmpeg. 2.0.143.
* v2.0, 2015-03-15, start [2.0release branch][branch2], 80773 lines.
* v2.0, 2015-03-14, fix [#324][bug #324], support hstrs(http stream trigger rtmp source) edge mode. 2.0.140.
* v2.0, 2015-03-14, for [#324][bug #324], support hstrs(http stream trigger rtmp source) origin mode. 2.0.139.
* v2.0, 2015-03-12, fix [#328][bug #328], support adobe hds. 2.0.138.
* v2.0, 2015-03-10, fix [#155][bug #155], support osx(darwin) for mac pro. 2.0.137.
* v2.0, 2015-03-08, fix [#316][bug #316], http api provides stream/vhost/srs/server bytes, codec and count. 2.0.136.
* v2.0, 2015-03-08, fix [#310][bug #310], refine aac LC, support aac HE/HEv2. 2.0.134.
* v2.0, 2015-03-06, for [#322][bug #322], fix http-flv stream bug, support multiple streams. 2.0.133.
* v2.0, 2015-03-06, refine http request parse. 2.0.132.
* v2.0, 2015-03-01, for [#179][bug #179], revert dvr http api. 2.0.128.
* v2.0, 2015-02-24, for [#304][bug #304], fix hls bug, write pts/dts error. 2.0.124
* v2.0, 2015-02-19, refine dvr, append file when dvr file exists. 2.0.122.
* v2.0, 2015-02-19, refine pithy print to more easyer to use. 2.0.121.
* v2.0, 2015-02-18, fix [#133][bug #133], support push rtsp to srs. 2.0.120.
* v2.0, 2015-02-17, the join maybe failed, should use a variable to ensure thread terminated. 2.0.119.
* v2.0, 2015-02-15, for [#304][bug #304], support config default acodec/vcodec. 2.0.118.
* v2.0, 2015-02-15, for [#304][bug #304], rewrite hls/ts code, support h.264+mp3 for hls. 2.0.117.
* v2.0, 2015-02-12, for [#304][bug #304], use stringstream to generate m3u8, add hls_td_ratio. 2.0.116.
* v2.0, 2015-02-11, dev code ZhouGuowen for 2.0.115.
* v2.0, 2015-02-10, for [#311][bug #311], set pcr_base to dts. 2.0.114.
* v2.0, 2015-02-10, fix [the bug][p21] of ibmf format which decoded in annexb.
* v2.0, 2015-02-10, for [#310][bug #310], downcast aac SSR to LC. 2.0.113
* v2.0, 2015-02-03, fix [#136][bug #136], support hls without io(in ram). 2.0.112
* v2.0, 2015-01-31, for [#250][bug #250], support push MPEGTS over UDP to SRS. 2.0.111
* v2.0, 2015-01-29, build libfdk-aac in ffmpeg. 2.0.108
* v2.0, 2015-01-25, for [#301][bug #301], hls support h.264+mp3, ok for vlc. 2.0.107
* v2.0, 2015-01-25, for [#301][bug #301], http ts stream support h.264+mp3. 2.0.106
* v2.0, 2015-01-25, hotfix [#268][bug #268], refine the pcr start at 0, dts/pts plus delay. 2.0.105
* v2.0, 2015-01-25, hotfix [#151][bug #151], refine pcr=dts-800ms and use dts/pts directly. 2.0.104
* v2.0, 2015-01-23, hotfix [#151][bug #151], use absolutely overflow to make jwplayer happy. 2.0.103
* v2.0, 2015-01-22, for [#293][bug #293], support http live ts stream. 2.0.101.
* v2.0, 2015-01-19, for [#293][bug #293], support http live flv/aac/mp3 stream with fast cache. 2.0.100.
* v2.0, 2015-01-18, for [#293][bug #293], support rtmp remux to http flv live stream. 2.0.99.
* v2.0, 2015-01-17, fix [#277][bug #277], refine http server refer to go http-framework. 2.0.98
* v2.0, 2015-01-17, for [#277][bug #277], refine http api refer to go http-framework. 2.0.97
* v2.0, 2015-01-17, hotfix [#290][bug #290], use iformat only for rtmp input. 2.0.95
* v2.0, 2015-01-08, hotfix [#281][bug #281], fix hls bug ignore type-9 send aud. 2.0.93
* v2.0, 2015-01-03, fix [#274][bug #274], http-callback support on_dvr when reap a dvr file. 2.0.89
* v2.0, 2015-01-03, hotfix to remove the pageUrl for http callback. 2.0.88
* v2.0, 2015-01-03, fix [#179][bug #179], dvr support custom filepath by variables. 2.0.87
* v2.0, 2015-01-02, fix [#211][bug #211], support security allow/deny publish/play all/ip. 2.0.86
* v2.0, 2015-01-02, hotfix [#207][bug #207], trim the last 0 of log. 2.0.85
* v2.0, 2014-01-02, fix [#158][bug #158], http-callback check http status code ok(200). 2.0.84
* v2.0, 2015-01-02, hotfix [#216][bug #216], http-callback post in application/json content-type. 2.0.83
* v2.0, 2014-01-02, fix [#263][bug #263], srs-librtmp flv read tag should init size. 2.0.82
* v2.0, 2015-01-01, hotfix [#270][bug #270], memory leak for http client post. 2.0.81
* v2.0, 2014-12-12, fix [#266][bug #266], aac profile is object id plus one. 2.0.80
* v2.0, 2014-12-29, hotfix [#267][bug #267], the forward dest ep should use server. 2.0.79
* v2.0, 2014-12-29, hotfix [#268][bug #268], the hls pcr is negative when startup. 2.0.78
* v2.0, 2014-12-22, hotfix [#264][bug #264], ignore NALU when sequence header to make HLS happy. 2.0.76
* v2.0, 2014-12-20, hotfix [#264][bug #264], support disconnect publish connect when hls error. 2.0.75
* v2.0, 2014-12-12, fix [#257][bug #257], support 0.1s+ latency. 2.0.70
* v2.0, 2014-12-08, update wiki for mr([EN][v3_EN_LowLatency#merged-read], [CN][v3_CN_LowLatency#merged-read]) and mw([EN][v3_EN_LowLatency#merged-write], [CN][v3_CN_LowLatency#merged-write]).
* v2.0, 2014-12-07, fix [#251][bug #251], 10k+ clients, use queue cond wait and fast vector. 2.0.67
* v2.0, 2014-12-05, fix [#251][bug #251], 9k+ clients, use fast cache for msgs queue. 2.0.57
* v2.0, 2014-12-04, fix [#241][bug #241], add mw(merged-write) config. 2.0.53
* v2.0, 2014-12-04, for [#241][bug #241], support mr(merged-read) config and reload. 2.0.52.
* v2.0, 2014-12-04, enable [#241][bug #241] and [#248][bug #248], +25% performance, 2.5k publisher. 2.0.50
* v2.0, 2014-12-04, fix [#248][bug #248], improve about 15% performance for fast buffer. 2.0.49
* v2.0, 2014-12-03, fix [#244][bug #244], conn thread use cond to wait for recv thread error. 2.0.47.
* v2.0, 2014-12-02, merge [#239][p23], traverse the token before response connect. 2.0.45.
* v2.0, 2014-12-02, srs-librtmp support hijack io apis for st-load. 2.0.42.
* v2.0, 2014-12-01, for [#237][bug #237], refine syscall for recv, supports 1.5k clients. 2.0.41.
* v2.0, 2014-11-30, add qtcreate project file trunk/src/qt/srs/srs-qt.pro. 2.0.39.
* v2.0, 2014-11-29, fix [#235][bug #235], refine handshake, replace union with template method. 2.0.38.
* v2.0, 2014-11-28, fix [#215][bug #215], add srs_rtmp_dump tool. 2.0.37.
* v2.0, 2014-11-25, update PRIMARY, AUTHORS, CONTRIBUTORS rule. 2.0.32.
* v2.0, 2014-11-24, fix [#212][bug #212], support publish aac adts raw stream. 2.0.31.
* v2.0, 2014-11-22, fix [#217][bug #217], remove timeout recv, support 7.5k+ 250kbps clients. 2.0.30.
* v2.0, 2014-11-21, srs-librtmp add rtmp prefix for rtmp/utils/human apis. 2.0.29.
* v2.0, 2014-11-21, refine examples of srs-librtmp, add srs_print_rtmp_packet. 2.0.28.
* v2.0, 2014-11-20, fix [#212][bug #212], support publish audio raw frames. 2.0.27
* v2.0, 2014-11-19, fix [#213][bug #213], support compile [srs-librtmp on windows](https://github.com/winlinvip/srs.librtmp), [bug #213][bug #213]. 2.0.26
* v2.0, 2014-11-18, all wiki translated to English. 2.0.23.
* v2.0, 2014-11-15, fix [#204][bug #204], srs-librtmp drop duplicated sps/pps(sequence header). 2.0.22.
* v2.0, 2014-11-15, fix [#203][bug #203], srs-librtmp drop any video before sps/pps(sequence header). 2.0.21.
* v2.0, 2014-11-15, fix [#202][bug #202], fix memory leak of h.264 raw packet send in srs-librtmp. 2.0.20.
* v2.0, 2014-11-13, fix [#200][bug #200], deadloop when read/write 0 and ETIME. 2.0.16.
* v2.0, 2014-11-13, fix [#194][bug #194], writev multiple msgs, support 6k+ 250kbps clients. 2.0.15.
* v2.0, 2014-11-12, fix [#194][bug #194], optmized st for timeout recv. pulse to 500ms. 2.0.14.
* v2.0, 2014-11-11, fix [#195][bug #195], remove the confuse code st_usleep(0). 2.0.13.
* v2.0, 2014-11-08, fix [#191][bug #191], configure --export-librtmp-project and --export-librtmp-single. 2.0.11.
* v2.0, 2014-11-08, fix [#66][bug #66], srs-librtmp support write h264 raw packet. 2.0.9.
* v2.0, 2014-10-25, fix [#185][bug #185], AMF0 support 0x0B the date type codec. 2.0.7.
* v2.0, 2014-10-24, fix [#186][bug #186], hotfix for bug #186, drop connect args when not object. 2.0.6.
* v2.0, 2014-10-24, rename wiki/xxx to wiki/v1_CN_xxx. 2.0.3.
* v2.0, 2014-10-19, fix [#184][bug #184], support AnnexB in RTMP body for HLS. 2.0.2
* v2.0, 2014-10-18, remove supports for OSX(darwin). 2.0.1.
* v2.0, 2014-10-16, revert github srs README to English. 2.0.0.

### V1 changes

* <strong>v1.0, 2014-12-05, [1.0 release(1.0.10)][r1.0r0] released. 59391 lines.</strong>
* <strong>v1.0, 2014-10-09, [1.0 beta(1.0.0)][r1.0b0] released. 59316 lines.</strong>
* v1.0, 2014-10-08, fix [#151][bug #151], always reap ts whatever audio or video packet. 0.9.223.
* v1.0, 2014-10-08, fix [#162][bug #162], failed if no epoll. 0.9.222.
* v1.0, 2014-09-30, fix [#180][bug #180], crash for multiple edge publishing the same stream. 0.9.220.
* v1.0, 2014-09-26, fix hls bug, refine config and log, according to clion of jetbrains. 0.9.216. 
* v1.0, 2014-09-25, fix [#177][bug #177], dvr segment add config dvr_wait_keyframe. 0.9.213.
* v1.0, 2014-08-28, fix [#167][bug #167], add openssl includes to utest. 0.9.209.
* v1.0, 2014-08-27, max connections is 32756, for st use mmap default. 0.9.209
* v1.0, 2014-08-24, fix [#150][bug #150], forward should forward the sequence header when retry. 0.9.208.
* v1.0, 2014-08-22, for [#165][bug #165], refine dh wrapper, ensure public key is 128bytes. 0.9.206.
* v1.0, 2014-08-19, for [#160][bug #160], support forward/edge to flussonic, disable debug_srs_upnode to make flussonic happy. 0.9.201.
* v1.0, 2014-08-17, for [#155][bug #155], refine for osx, with ssl/http, disable statistics. 0.9.198.
* v1.0, 2014-08-06, fix [#148][bug #148], simplify the RTMP handshake key generation. 0.9.191.
* v1.0, 2014-08-06, fix [#147][bug #147], support identify the srs edge. 0.9.190.
* <strong>v1.0, 2014-08-03, [1.0 mainline7(0.9.189)][r1.0a7] released. 57432 lines.</strong>
* v1.0, 2014-08-03, fix [#79][bug #79], fix the reload remove edge assert bug. 0.9.189.
* v1.0, 2014-08-03, fix [#57][bug #57], use lock(acquire/release publish) to avoid duplicated publishing. 0.9.188.
* v1.0, 2014-08-03, fix [#85][bug #85], fix the segment-dvr sequence header missing. 0.9.187.
* v1.0, 2014-08-03, fix [#145][bug #145], refine ffmpeg log, check abitrate for libaacplus. 0.9.186.
* v1.0, 2014-08-03, fix [#143][bug #143], fix retrieve sys stat bug for all linux. 0.9.185.
* v1.0, 2014-08-02, fix [#138][bug #138], fix http hooks bug, regression bug. 0.9.184.
* v1.0, 2014-08-02, fix [#142][bug #142], fix tcp stat slow bug, use /proc/net/sockstat instead, refer to 'ss -s'. 0.9.183.
* v1.0, 2014-07-31, fix [#141][bug #141], support tun0(vpn network device) ip retrieve. 0.9.179.
* v1.0, 2014-07-27, support partially build on OSX(Darwin). 0.9.177
* v1.0, 2014-07-27, api connections add udp, add disk iops. 0.9.176
* v1.0, 2014-07-26, complete config utest. 0.9.173
* v1.0, 2014-07-26, fix [#124][bug #124], gop cache support disable video in publishing. 0.9.171.
* v1.0, 2014-07-23, fix [#121][bug #121], srs_info detail log compile failed. 0.9.168.
* v1.0, 2014-07-19, fix [#119][bug #119], use iformat and oformat for ffmpeg transcode. 0.9.163.
* <strong>v1.0, 2014-07-13, [1.0 mainline6(0.9.160)][r1.0a6] released. 50029 lines.</strong>
* v1.0, 2014-07-13, refine the bandwidth check/test, add as/js library, use srs-librtmp for linux tool. 0.9.159
* v1.0, 2014-07-12, complete rtmp stack utest. 0.9.156
* v1.0, 2014-07-06, fix [#81][bug #81], fix HLS codec info, IOS ok. 0.9.153.
* v1.0, 2014-07-06, fix [#103][bug #103], support all aac sample rate. 0.9.150.
* v1.0, 2014-07-05, complete kernel utest. 0.9.149
* v1.0, 2014-06-30, fix [#111][bug #111], always use 31bits timestamp. 0.9.143.
* v1.0, 2014-06-28, response the call message with null. 0.9.137
* v1.0, 2014-06-28, fix [#110][bug #110], thread start segment fault, thread cycle stop destroy thread. 0.9.136
* v1.0, 2014-06-27, fix [#109][bug #109], fix the system jump time, adjust system startup time. 0.9.135
* <strong>v1.0, 2014-06-27, [1.0 mainline5(0.9.134)][r1.0a5] released. 41573 lines.</strong>
* v1.0, 2014-06-27, SRS online 30days with RTMP/HLS.
* v1.0, 2014-06-25, fix [#108][bug #108], support config time jitter for encoder non-monotonical stream. 0.9.133
* v1.0, 2014-06-23, support report summaries in heartbeat. 0.9.132
* v1.0, 2014-06-22, performance refine, support [3k+][v1_CN_Performance#performancereport4k] connections(270kbps). 0.9.130
* v1.0, 2014-06-21, support edge [token traverse][v3_CN_DRM#tokentraverse], fix [#104][bug #104]. 0.9.129
* v1.0, 2014-06-19, add connections count to api summaries. 0.9.127
* v1.0, 2014-06-19, add srs bytes and kbps to api summaries. 0.9.126
* v1.0, 2014-06-18, add network bytes to api summaries. 0.9.125
* v1.0, 2014-06-14, fix [#98][bug #98], workaround for librtmp ping(fmt=1,cid=2 fresh stream). 0.9.124
* v1.0, 2014-05-29, support flv inject and flv http streaming with start=bytes. 0.9.122
* <strong>v1.0, 2014-05-28, [1.0 mainline4(0.9.120)][r1.0a4] released. 39200 lines.</strong>
* v1.0, 2014-05-27, fix [#87][bug #87], add source id for full trackable log. 0.9.120
* v1.0, 2014-05-27, fix [#84][bug #84], unpublish when edge disconnect. 0.9.119
* v1.0, 2014-05-27, fix [#89][bug #89], config to /dev/null to disable ffmpeg log. 0.9.117
* v1.0, 2014-05-25, fix [#76][bug #76], allow edge vhost to add or remove. 0.9.114
* v1.0, 2014-05-24, Johnny contribute [ossrs.net](http://ossrs.net). karthikeyan start to translate wiki to English.
* v1.0, 2014-05-22, fix [#78][bug #78], st joinable thread must be stop by other threads, 0.9.113
* v1.0, 2014-05-22, support amf0 StrictArray(0x0a). 0.9.111.
* v1.0, 2014-05-22, support flv parser, add amf0 to librtmp. 0.9.110
* v1.0, 2014-05-22, fix [#74][bug #74], add tcUrl for http callback on_connect, 0.9.109
* v1.0, 2014-05-19, support http heartbeat, 0.9.107
* <strong>v1.0, 2014-05-18, [1.0 mainline3(0.9.105)][r1.0a3] released. 37594 lines.</strong>
* v1.0, 2014-05-18, support http api json, to PUT/POST. 0.9.105
* v1.0, 2014-05-17, fix [#72][bug #72], also need stream_id for send_and_free_message. 0.9.101
* v1.0, 2014-05-17, rename struct to class. 0.9.100
* v1.0, 2014-05-14, fix [#67][bug #67] pithy print, stage must has a age. 0.9.98
* v1.0, 2014-05-13, fix mem leak for delete[] SharedPtrMessage array. 0.9.95
* v1.0, 2014-05-12, refine the kbps calc module. 0.9.93
* v1.0, 2014-05-12, fix bug [#64][bug #64]: install_dir=DESTDIR+PREFIX
* v1.0, 2014-05-08, fix [#36][bug #36]: never directly use \*(int32_t\*) for arm.
* v1.0, 2014-05-08, fix [#60][bug #60]: support aggregate message
* v1.0, 2014-05-08, fix [#59][bug #59], edge support FMS origin server. 0.9.92
* v1.0, 2014-05-06, fix [#50][bug #50], ubuntu14 build error.
* v1.0, 2014-05-04, support mips linux.
* v1.0, 2014-04-30, fix bug [#34][bug #34]: convert signal to io thread. 0.9.85
* v1.0, 2014-04-29, refine RTMP protocol completed, to 0.9.81
* <strong>v1.0, 2014-04-28, [1.0 mainline2(0.9.79)][r1.0a2] released. 35255 lines.</strong>
* v1.0, 2014-04-28, support full edge RTMP server. 0.9.79
* v1.0, 2014-04-27, support basic edge(play/publish) RTMP server. 0.9.78
* v1.0, 2014-04-25, add donation page. 0.9.76
* v1.0, 2014-04-21, support android app to start srs for internal edge. 0.9.72
* v1.0, 2014-04-19, support tool over srs-librtmp to ingest flv/rtmp. 0.9.71
* v1.0, 2014-04-17, support dvr(record live to flv file for vod). 0.9.69
* v1.0, 2014-04-11, add speex1.2 to transcode flash encoder stream. 0.9.58
* v1.0, 2014-04-10, support reload ingesters(add/remov/update). 0.9.57
* <strong>v1.0, 2014-04-07, [1.0 mainline(0.9.55)][r1.0a0] released. 30000 lines.</strong>
* v1.0, 2014-04-07, support [ingest][v1_CN_SampleIngest] file/stream/device.
* v1.0, 2014-04-05, support [http api][v3_CN_HTTPApi] and [http server][v2_CN_HTTPServer].
* v1.0, 2014-04-03, implements http framework and api/v1/version.
* v1.0, 2014-03-30, fix bug for st detecting epoll failed, force st to use epoll.
* v1.0, 2014-03-29, add wiki [Performance for RaspberryPi][v1_CN_RaspberryPi].
* v1.0, 2014-03-29, add release binary package for raspberry-pi. 
* v1.0, 2014-03-26, support RTMP ATC for HLS/HDS to support backup(failover).
* v1.0, 2014-03-23, support daemon, default start in daemon.
* v1.0, 2014-03-22, support make install/install-api and uninstall.
* v1.0, 2014-03-22, add ./etc/init.d/srs, refine to support make clean then make.
* v1.0, 2014-03-21, write pid to ./objs/srs.pid.
* v1.0, 2014-03-20, refine hls code, support pure audio HLS.
* v1.0, 2014-03-19, add vn/an for FFMPEG to drop video/audio for radio stream.
* v1.0, 2014-03-19, refine handshake, client support complex handshake, add utest.
* v1.0, 2014-03-16, fix bug on arm of st, the sp change from 20 to 8, for respberry-pi, @see [commit][p22]
* v1.0, 2014-03-16, support ARM([debian armhf, v7cpu][v1_CN_SrsLinuxArm]) with rtmp/ssl/hls/librtmp.
* v1.0, 2014-03-12, finish utest for amf0 codec.
* v1.0, 2014-03-06, add gperftools for mem leak detect, mem/cpu profile.
* v1.0, 2014-03-04, add gest framework for utest, build success.
* v1.0, 2014-03-02, add wiki [srs-librtmp][v3_CN_SrsLibrtmp], [SRS for arm][v1_CN_SrsLinuxArm], [product][v1_CN_Product]
* v1.0, 2014-03-02, srs-librtmp, client publish/play library like librtmp.
* v1.0, 2014-03-01, modularity, extract core/kernel/rtmp/app/main module.
* v1.0, 2014-02-28, support arm build(SRS/ST), add ssl to 3rdparty package.
* v1.0, 2014-02-28, add wiki [BuildArm][v3_CN_Build], [FFMPEG][v3_CN_FFMPEG], [Reload][v1_CN_Reload]
* v1.0, 2014-02-27, add wiki [LowLatency][v3_CN_LowLatency], [HTTPCallback][v3_CN_HTTPCallback], [ServerSideScript][v1_CN_ServerSideScript], [IDE][v2_CN_IDE]
* v1.0, 2014-01-19, add wiki [DeliveryHLS][v2_CN_DeliveryHLS]
* v1.0, 2014-01-12, add wiki [HowToAskQuestion][v1_CN_HowToAskQuestion], [RtmpUrlVhost][v1_CN_RtmpUrlVhost]
* v1.0, 2014-01-11, fix jw/flower player pause bug, which send closeStream actually.
* v1.0, 2014-01-05, add wiki [Build][v3_CN_Build], [Performance][v1_CN_Performance], [Forward][v3_CN_Forward]
* v1.0, 2014-01-01, change listen(512), chunk-size(60000), to improve performance.
* v1.0, 2013-12-27, merge from wenjie, the bandwidth test feature.
* <strong>v0.9, 2013-12-25, [v0.9][r0.9] released. 20926 lines.</strong>
* v0.9, 2013-12-25, fix the bitrate bug(in Bps), use enhanced microphone.
* v0.9, 2013-12-22, demo video meeting or chat(SRS+cherrypy+jquery+bootstrap).
* v0.9, 2013-12-22, merge from wenjie, support banwidth test.
* v0.9, 2013-12-22, merge from wenjie: support set chunk size at vhost level
* v0.9, 2013-12-21, add [players][player] for play and publish.
* v0.9, 2013-12-15, ensure the HLS(ts) is continous when republish stream.
* v0.9, 2013-12-15, fix the hls reload bug, feed it the sequence header.
* v0.9, 2013-12-15, refine protocol, use int64_t timestamp for ts and jitter.
* v0.9, 2013-12-15, support set the live queue length(in seconds), drop when full.
* v0.9, 2013-12-15, fix the forwarder reconnect bug, feed it the sequence header.
* v0.9, 2013-12-15, support reload the hls/forwarder/transcoder.
* v0.9, 2013-12-14, refine the thread model for the retry threads.
* v0.9, 2013-12-10, auto install depends tools/libs on centos/ubuntu.
* <strong>v0.8, 2013-12-08, [v0.8][r0.8] released. 19186 lines.</strong>
* v0.8, 2013-12-08, support [http hooks][v3_CN_HTTPCallback]: on_connect/close/publish/unpublish/play/stop.
* v0.8, 2013-12-08, support multiple http hooks for a event.
* v0.8, 2013-12-07, support http callback hooks, on_connect.
* v0.8, 2013-12-07, support network based cli and json result, add CherryPy 3.2.4.
* v0.8, 2013-12-07, update http/hls/rtmp load test tool [SB][srs-bench], use SRS rtmp sdk.
* v0.8, 2013-12-06, support max_connections, drop if exceed.
* v0.8, 2013-12-05, support log_dir, write ffmpeg log to file.
* v0.8, 2013-12-05, fix the forward/hls/encoder bug.
* <strong>v0.7, 2013-12-03, [v0.7][r0.7] released. 17605 lines.</strong>
* v0.7, 2013-12-01, support dead-loop detect for forwarder and transcoder.
* v0.7, 2013-12-01, support all ffmpeg filters and params.
* v0.7, 2013-11-30, support live stream transcoder by ffmpeg.
* v0.7, 2013-11-30, support --with/without -ffmpeg, build ffmpeg-2.1.
* v0.7, 2013-11-30, add ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
* <strong>v0.6, 2013-11-29, [v0.6][r0.6] released. 16094 lines.</strong>
* v0.6, 2013-11-29, add performance summary, 1800 clients, 900Mbps, CPU 90.2%, 41MB.
* v0.6, 2013-11-29, support forward stream to other edge server.
* v0.6, 2013-11-29, support forward stream to other origin server.
* v0.6, 2013-11-28, fix memory leak bug, aac decode bug.
* v0.6, 2013-11-27, support --with or --without -hls and -ssl options.
* v0.6, 2013-11-27, support AAC 44100HZ sample rate for iphone, adjust the timestamp.
* <strong>v0.5, 2013-11-26, [v0.5][r0.5] released. 14449 lines.</strong>
* v0.5, 2013-11-24, support HLS(m3u8), fragment and window.
* v0.5, 2013-11-24, support record to ts file for HLS.
* v0.5, 2013-11-21, add ts_info tool to demux ts file.
* v0.5, 2013-11-16, add rtmp players(OSMF/jwplayer5/jwplayer6).
* <strong>v0.4, 2013-11-10, [v0.4][r0.4] released. 12500 lines.</strong>
* v0.4, 2013-11-10, support config and reload the pithy print.
* v0.4, 2013-11-09, support reload config(vhost and its detail).
* v0.4, 2013-11-09, support reload config(listen and chunk_size) by SIGHUP(1).
* v0.4, 2013-11-09, support longtime(>4.6hours) publish/play.
* v0.4, 2013-11-09, support config the chunk_size.
* v0.4, 2013-11-09, support pause for live stream.
* <strong>v0.3, 2013-11-04, [v0.3][r0.3] released. 11773 lines.</strong>
* v0.3, 2013-11-04, support refer/play-refer/publish-refer.
* v0.3, 2013-11-04, support vhosts specified config.
* v0.3, 2013-11-02, support listen multiple ports.
* v0.3, 2013-11-02, support config file in nginx-conf style.
* v0.3, 2013-10-29, support pithy print log message specified by stage.
* v0.3, 2013-10-28, support librtmp without extended-timestamp in 0xCX chunk packet.
* v0.3, 2013-10-27, support cache last gop for client fast startup.
* <strong>v0.2, 2013-10-25, [v0.2][r0.2] released. 10125 lines.</strong>
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake.
* v0.2, 2013-10-24, support time jitter detect and correct algorithm
* v0.2, 2013-10-24, support decode codec type to cache the h264/avc sequence header.
* <strong>v0.1, 2013-10-23, [v0.1][r0.1] released. 8287 lines.</strong>
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send\_message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv\_message).

### Releases

* 2016-11-13, [Release v2.0-b3][r2.0b3], 2.0 beta3, 2.0.223, 86685 lines.
* 2016-11-09, [Release v2.0-b2][r2.0b2], 2.0 beta2, 2.0.221, 86691 lines.
* 2016-09-09, [Release v2.0-b1][r2.0b1], 2.0 beta1, 2.0.215, 89941 lines.
* 2016-08-06, [Release v2.0-b0][r2.0b0], 2.0 beta0, 2.0.210, 89704 lines.
* 2015-12-23, [Release v2.0-a3][r2.0a3], 2.0 alpha3, 2.0.205, 89544 lines.
* 2015-10-08, [Release v2.0-a2][r2.0a2], 2.0 alpha2, 2.0.195, 89358 lines.
* 2015-09-14, [Release v2.0-a1][r2.0a1], 2.0 alpha1, 2.0.189, 89269 lines.
* 2015-08-23, [Release v2.0-a0][r2.0a0], 2.0 alpha0, 2.0.185, 89022 lines.
* 2015-05-23, [Release v1.0-r4][r1.0r4], bug fixed, 1.0.32, 59509 lines.
* 2015-03-19, [Release v1.0-r3][r1.0r3], bug fixed, 1.0.30, 59511 lines.
* 2015-02-12, [Release v1.0-r2][r1.0r2], bug fixed, 1.0.27, 59507 lines.
* 2015-01-15, [Release v1.0-r1][r1.0r1], bug fixed, 1.0.21, 59472 lines.
* 2014-12-05, [Release v1.0-r0][r1.0r0], all bug fixed, 1.0.10, 59391 lines.
* 2014-10-09, [Release v0.9.8][r1.0b0], all bug fixed, 1.0.0, 59316 lines.
* 2014-08-03, [Release v0.9.7][r1.0a7], config utest, all bug fixed. 57432 lines.
* 2014-07-13, [Release v0.9.6][r1.0a6], core/kernel/rtmp utest, refine bandwidth(as/js/srslibrtmp library). 50029 lines.
* 2014-06-27, [Release v0.9.5][r1.0a5], refine perf 3k+ clients, edge token traverse, [srs monitor](http://ossrs.net:1977), 30days online. 41573 lines.
* 2014-05-28, [Release v0.9.4][r1.0a4], support heartbeat, tracable log, fix mem leak and bugs. 39200 lines.
* 2014-05-18, [Release v0.9.3][r1.0a3], support mips, fms origin, json(http-api). 37594 lines.
* 2014-04-28, [Release v0.9.2][r1.0a2], support [dvr][v3_CN_DVR], android, [edge][v3_CN_Edge]. 35255 lines.
* 2014-04-07, [Release v0.9.1][r1.0a0], support [arm][v1_CN_SrsLinuxArm], [init.d][v1_CN_LinuxService], http [server][v2_CN_HTTPServer]/[api][v3_CN_HTTPApi], [ingest][v1_CN_SampleIngest]. 30000 lines.
* 2013-12-25, [Release v0.9.0][r0.9], support bandwidth test, player/encoder/chat [demos][v1_CN_SampleDemo]. 20926 lines.
* 2013-12-08, [Release v0.8.0][r0.8], support [http hooks callback][v3_CN_HTTPCallback], update [SB][srs-bench]. 19186 lines.
* 2013-12-03, [Release v0.7.0][r0.7], support [live stream transcoding][v3_CN_FFMPEG]. 17605 lines.
* 2013-11-29, [Release v0.6.0][r0.6], support [forward][v3_CN_Forward] stream to origin/edge. 16094 lines.
* 2013-11-26, [Release v0.5.0][r0.5], support [HLS(m3u8)][v2_CN_DeliveryHLS], fragment and window. 14449 lines.
* 2013-11-10, [Release v0.4.0][r0.4], support [reload][v1_CN_Reload] config, pause, longtime publish/play. 12500 lines.
* 2013-11-04, [Release v0.3.0][r0.3], support [vhost][v1_CN_RtmpUrlVhost], refer, gop cache, listen multiple ports. 11773 lines.
* 2013-10-25, [Release v0.2.0][r0.2], support [rtmp][v1_CN_RTMPHandshake] flash publish, h264, time jitter correct. 10125 lines.
* 2013-10-23, [Release v0.1.0][r0.1], support [rtmp FMLE/FFMPEG publish][v1_CN_DeliveryRTMP], vp6. 8287 lines.
* 2013-10-17, Created.

### Compare

Compare SRS with other media server.

#### Stream Delivery

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   RTMP        |   Stable  |   Stable  |   Stable  |   Stable  |   Stable  |
|   HLS         |   Stable  |   Stable  |   X       |   Stable  |   Stable  |
|   HDS         | Experiment|   X       |   X       |   Stable  |   Stable  |
|   HTTP FLV    |   Stable  |   X       |   X       |   X       |   X       |
|   HLS(aonly)  |   Stable  |   X       |   X       |   Stable  |   Stable  |
|   HTTP Server |   Stable  |   Stable  |   X       |   X       |   Stable  |

#### Cluster

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   RTMP Edge   |   Stable  |   X       |   X       |   Stable  |   X       |
|   RTMP Backup |   Stable  |   X       |   X       |   X       |   X       |
|   VHOST       |   Stable  |   X       |   X       |   Stable  |   Stable  |
|   Reload      |   Stable  |   X       |   X       |   X       |   X       |
|   Forward     |   Stable  |   X       |   X       |   X       |   X       |
|   ATC         |   Stable  |   X       |   X       |   X       |   X       |

#### Stream Service

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   DVR         |   Stable  |   Stable  |   X       |   X       |   Stable  |
|   DVR API     |   Stable  |   Stable  |   X       |   X       |   X       |
|   EXEC        |   Stable  |   Stable  |   X       |   X       |   X       |
|   Transcode   |   Stable  |   X       |   X       |   X       |   Stable  |
|   HTTP API    |   Stable  |   Stable  |   X       |   X       |   Stable  |
| HTTP RAW API  |   Stable  |   X       |   X       |   X       |   X       |
|   HTTP hooks  |   Stable  |   X       |   X       |   X       |   X       |
|   GopCache    |   Stable  |   X       |   X       |   Stable  |   X       |
|   Security    |   Stable  |   Stable  |   X       |   X       |   Stable  |
| Token Traverse|   Stable  |   X       |   X       |   Stable  |   X       |

#### Efficiency

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   Concurrency |   7.5k    |   3k      |   2k      |   2k      |   3k      |
|MultipleProcess| Experiment|   Stable  |   X       |   X       |   X       |
|   RTMP Latency|   0.1s    |   3s      |   3s      |   3s      |   3s      |
|   HLS Latency |   10s     |   30s     |   X       |   30s     |   30s     |

#### Stream Caster

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   Ingest      |   Stable  |   X       |   X       |   X       |   X       |
|   Push MPEGTS | Experiment|   X       |   X       |   X       |   Stable  |
|   Push RTSP   | Experiment|   X       |   Stable  |   X       |   Stable  |
| Push HTTP FLV | Experiment|   X       |   X       |   X       |   X       |

#### Debug System

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   BW check    |   Stable  |   X       |   X       |   X       |   X       |
| Tracable Log  |   Stable  |   X       |   X       |   X       |   X       |

#### Docs

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   Demos       |   Stable  |   X       |   X       |   X       |   X       |
|   WIKI(EN+CN) |   Stable  |  EN only  |   X       |   X       |   Stable  |

#### Others 

|   Feature     |   SRS     |   NGINX   | CRTMPD    | FMS       |   WOWZA   |
|   ----------- |   ------- |   -----   | --------- | --------  |   ------  |
|   ARM/MIPS    |   Stable  |   Stable  |   X       |   X       |   X       |
| Client Library|   Stable  |   X       |   X       |   X       |   X       |

Remark:

1. Concurrency: The concurrency of single process.
1. MultipleProcess: SRS is single process, while [srs-dolphin][srs-dolphin] is a MultipleProcess SRS.
1. HLS aonly: The HLS audio only streaming delivery.
1. BW check: The bandwidth check.
1. Security: To allow or deny stream publish or play.
1. Reload: Nginx supports reload, but not nginx-rtmp.

### Performance

Performance benchmark history, on virtual box.

* See also: [Performance for x86/x64 Test Guide][v1_CN_Performance]
* See also: [Performance for RaspberryPi][v1_CN_RaspberryPi]
* About multiple-process performance, read [srs-dolphin][srs-dolphin].

#### Play RTMP benchmark

The play RTMP benchmark by [SB][srs-bench]:


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

#### Publish RTMP benchmark

The publish RTMP benchmark by [SB][srs-bench]:

|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2014-12-03  |   1.0.10  |   1.2k(1200)  |   publishers  |   96%     |   43MB    |   -           |
|   2014-12-03  |   2.0.12  |   1.2k(1200)  |   publishers  |   96%     |   43MB    |   -           |
|   2014-12-03  |   2.0.47  |   1.2k(1200)  |   publishers  |   84%     |   76MB    |   [code][p1]  |
|   2014-12-03  |   2.0.47  |   1.4k(1400)  |   publishers  |   95%     |   140MB   |   -           |
|   2014-12-03  |   2.0.48  |   1.4k(1400)  |   publishers  |   95%     |   140MB   |   [code][p2]  |
|   2014-12-04  |   2.0.49  |   1.4k(1400)  |   publishers  |   68%     |   144MB   |   -           |
|   2014-12-04  |   2.0.49  |   2.5k(2500)  |   publishers  |   95%     |   404MB   |   [code][p3]  |
|   2014-12-04  |   2.0.51  |   2.5k(2500)  |   publishers  |   91%     |   259MB   |   [code][p4]  |
|   2014-12-04  |   2.0.52  |   4.0k(4000)  |   publishers  |   80%     |   331MB   |   [code][p5]  |

#### Play HTTP FLV benchmark

The play HTTP FLV benchmark by [SB][srs-bench]:


|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2014-05-24  |   2.0.167 |   1.0k(1000)  |   players     |   82%     |   86MB    |   -           |
|   2014-05-24  |   2.0.168 |   2.3k(2300)  |   players     |   92%     |   276MB   |   [code][p17] |
|   2014-05-24  |   2.0.169 |   3.0k(3000)  |   players     |   94%     |   188MB   |   [code][p18] |
|   2014-05-24  |   2.0.170 |   3.0k(3000)  |   players     |   89%     |   96MB    |   [code][p19] |
|   2014-05-25  |   2.0.171 |   6.0k(6000)  |   players     |   84%     |   297MB   |   [code][p20] |

#### Latency benchmark

The latency between encoder and player with realtime config([CN][v3_CN_LowLatency], [EN][v3_EN_LowLatency]):
|   

|   Update      |    SRS    |    VP6    |  H.264    |  VP6+MP3  | H.264+MP3 |
| ------------- | --------- | --------- | --------- | --------- | --------  |
|   2014-12-03  |   1.0.10  |   0.4s    |   0.4s    |   0.9s    |   1.2s    |
|   2014-12-12  |   2.0.70  |[0.1s][p13]|[0.4s][p14]|   1.0s    |   0.9s    |
|   2014-12-16  |   2.0.72  |   0.1s    |   0.4s    |[0.8s][p15]|[0.6s][p16]|

We use FMLE as encoder for benchmark. The latency of server is 0.1s+, 
and the bottleneck is the encoder. For more information, read 
[bug #257][bug #257-c0].

#### HLS overhead

About the HLS overhead of SRS, we compare the overhead to FLV by remux the HLS to FLV by ffmpeg.

| Bitrate   |   Duration    |   FLV(KB)     |   HLS(KB)     |   Overhead    |
| -------   |   --------    |   -------     |   --------    |   ---------   |
| 275kbps   |   600s        |   11144       |   12756       |   14.46%      |
| 260kbps   |   1860s       |   59344       |   68004       |   14.59%      |
| 697kbps   |   60s         |   5116        |   5476        |   7.03%       |
| 565kbps   |   453s        |   31316       |   33544       |   7.11%       |
| 565kbps   |   1813s       |   125224      |   134140      |   7.12%       |
| 861kbps   |   497s        |   52316       |   54924       |   4.98%       |
| 857kbps   |   1862s       |   195008      |   204768      |   5.00%       |
| 1301kbps  |   505s        |   80320       |   83676       |   4.17%       |
| 1312kbps  |   1915s       |   306920      |   319680      |   4.15%       |
| 2707kbps  |   600s        |   198356      |   204560      |   3.12%       |
| 2814kbps  |   1800s       |   618456      |   637660      |   3.10%       |
| 2828kbps  |   60s         |   20716       |   21356       |   3.08%       |
| 2599kbps  |   307s        |   97580       |   100672      |   3.16%       |
| 2640kbps  |   1283s       |   413880      |   426912      |   3.14%       |
| 5254kbps  |   71s         |   45832       |   47056       |   2.67%       |
| 5147kbps  |   370s        |   195040      |   200280      |   2.68%       |
| 5158kbps  |   1327s       |   835664      |   858092      |   2.68%       |

The HLS overhead is calc by: (HLS - FLV) / FLV * 100%.

The overhead is larger than this benchmark(48kbps audio is best overhead), for we fix the [#512][bug #512].

## Architecture

SRS always use the most simple architecture to support complex transaction.
* System arch: the system structure and arch.
* Modularity arch: the main modularity of SRS.
* Stream arch: the stream dispatch arch of SRS.

### System Architecture

```
+------------------------------------------------------+
|                    Application                       |
|            Origin/Edge/HTTP-FLV/StreamCaster         |
+---------------+---------------+-----------+----------+
|   RAW API/    |     EXEC/     |    DVR/   | FLV/TS/  |
|   API/hook    |   Transcoder  |  HLS/HDS  | AMF0/JSON|
+---------------+---------------+-----------+ RTMP/RTSP|
|  http-parser  |  FFMPEG/x264  |  NGINX/ts | protocol |
+---------------+---------------+-----------+----------+
|              Network(state-threads)                  |
+------------------------------------------------------+
|    All Linux/Unix(RHEL,CentOS,Ubuntu,Fedora...)      |
+------------------------------------------------------+
```

### Modularity Architecture

```
+------------------------------------------------------+
|             Main(srs/ingest-hls/librtmp)             |
+------------------------------------------------------+
|          Modules(1)(User defined modularity)         |
+------------------------------------------------------+
|           App(Server/Client application)             |
+------------------------------------------------------+
|         RTMP/HTTP/RTSP/RawStream(Protocol stack)     |
+------------------------------------------------------+
|      Kernel(depends on Core, provides error/log)     |
+------------------------------------------------------+
|         Core(depends only on system apis)            |
+------------------------------------------------------+
```

Remark:

1. Modules: SRS support embeded modularity, read [modules][modules].

### Stream Architecture

```
+---------+              +----------+
| Publish |              |  Deliver |
+---|-----+              +----|-----+
+----------------------+-------------------------+----------------+
|     Input            | SRS(Simple RTMP Server) |     Output     |
+----------------------+-------------------------+----------------+
|    Encoder(1)        |   +-> RTMP/HDS  --------+-> Flash player |
|  (FMLE,FFMPEG, -rtmp-+->-+-> HLS/HTTP ---------+-> M3U8 player  |
|  Flash,XSPLIT,       |   +-> FLV/MP3/Aac/Ts ---+-> HTTP player  |
|  ......)             |   +-> Fowarder ---------+-> RTMP server  |
|                      |   +-> Transcoder -------+-> RTMP server  |
|                      |   +-> EXEC(5) ----------+-> External app |
|                      |   +-> DVR --------------+-> FLV file     |
|                      |   +-> BandwidthTest ----+-> Flash        |
+----------------------+                         |                |
|  MediaSource(2)      |                         |                |
|  (RTSP,FILE,         |                         |                |
|   HTTP,HLS,   --pull-+->-- Ingester(3) -(rtmp)-+-> SRS          |
|   Device,            |                         |                |
|   ......)            |                         |                |
+----------------------+                         |                |
|  MediaSource(2)      |                         |                |
|  (RTSP,FILE,         |                         |                |
|   HTTP,HLS,   --push-+->-- Streamer(4) -(rtmp)-+-> SRS          |
|   Device,            |                         |                |
|   ......)            |                         |                |
+----------------------+-------------------------+----------------+

```

Remark:

1. Encoder: Encoder must push RTMP stream to SRS server.
1. MediaSource: Any media source, which can be ingest by ffmpeg.
1. Ingester: SRS fork a ffmpeg(or application) to ingest something to rtmp to SRS. Read [Ingest][v1_CN_Ingest].
1. Streamer: SRS listen to remux some protocol to rtmp to SRS. Read [Streamer][v2_CN_Streamer].
1. EXEC: SRS exec external application when got event, read [ng-exec][v3_CN_NgExec].

## Tips

Other tips...

### AUTHORS

There are two types of people that have contributed to the SRS project:
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

### Mirrors

Github: [https://github.com/ossrs/srs][srs], the GIT usage([CN][v1_CN_Git], [EN][v1_EN_Git])

```
git clone https://github.com/ossrs/srs.git
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

### System Requirements

Supported operating systems and hardware:
* All Linux , both 32 and 64 bits
* Apple OSX(Darwin), both 32 and 64bits.
* All hardware with x86/x86_64/arm/mips cpu.

Beijing, 2013.10<br/>
Winlin


[p1]: https://github.com/ossrs/srs/commit/787ab674e38734ea8e0678101614fdcd84645dc8
[p2]: https://github.com/ossrs/srs/commit/f35ec2155b1408d528a9f37da7904c9625186bcf
[p3]: https://github.com/ossrs/srs/commit/29324fab469e0f7cef9ad04ffdbce832ac7dd9ff
[p4]: https://github.com/ossrs/srs/commit/f57801eb46c16755b173984b915a4166922df6a6
[p5]: https://github.com/ossrs/srs/commit/5589b13d2e216b91f97afb78ee0c011b2fccf7da
[p6]: https://github.com/ossrs/srs/commit/1ae3e6c64cc5cee90e6050c26968ebc3c18281be
[p7]: https://github.com/ossrs/srs/commit/8acd143a7a152885b815999162660fd4e7a3f247
[p8]: https://github.com/ossrs/srs/commit/cc6aca9ad55342a06440ce7f3b38453776b2b2d1
[p9]: https://github.com/ossrs/srs/commit/58136ec178e3d47db6c90a59875d7e40946936e5
[p10]: https://github.com/ossrs/srs/commit/58136ec178e3d47db6c90a59875d7e40946936e5
[p11]: https://github.com/ossrs/srs/commit/9ee138746f83adc26f0e236ec017f4d68a300004
[p12]: https://github.com/ossrs/srs/commit/1311b6fe6576fd7b9c6d299b0f8f2e8d202f4bf8
[p13]: https://github.com/ossrs/srs/commit/10297fab519811845b549a8af40a6bcbd23411e8
[p14]: https://github.com/ossrs/srs/commit/10297fab519811845b549a8af40a6bcbd23411e8
[p15]: https://github.com/ossrs/srs/commit/0d6b91039d408328caab31a1077d56a809b6bebc
[p16]: https://github.com/ossrs/srs/commit/0d6b91039d408328caab31a1077d56a809b6bebc
[p17]: https://github.com/ossrs/srs/commit/fc995473eb02c7cf64b5b212b456e11f34aa7984
[p18]: https://github.com/ossrs/srs/commit/960341b9b2b9646270ccfd113b4dd784d9826c73
[p19]: https://github.com/ossrs/srs/commit/4df19ba99a4e4d80cd89b304f9298d343497bec9
[p20]: https://github.com/ossrs/srs/commit/d12fc7fcc5b2e9e3c8ee5c7da01d0e41c8f8ca4a
[p21]: https://github.com/ossrs/srs/commit/87519aaae835199e5adb60c0ae2c1cd24939448c
[p22]: https://github.com/ossrs/srs/commit/5a4373d4835758188b9a1f03005cea0b6ddc62aa
[p23]: https://github.com/ossrs/srs/pull/239
[pr #558]: https://github.com/ossrs/srs/pull/558
[pr #559]: https://github.com/ossrs/srs/pull/559

[authors]: https://github.com/ossrs/srs/blob/develop/AUTHORS.txt
[bigthanks]: https://github.com/ossrs/srs/wiki/v1_CN_Product#bigthanks
[st]: https://github.com/winlinvip/state-threads
[st2]: http://sourceforge.net/projects/state-threads/
[state-threads]: http://sourceforge.net/projects/state-threads/
[nginx-rtmp]: https://github.com/arut/nginx-rtmp-module
[http-parser]: https://github.com/joyent/http-parser
[nginx]: http://nginx.org/
[FFMPEG]: http://ffmpeg.org/
[libx264]: http://www.videolan.org/
[srs]: https://github.com/ossrs/srs
[csdn]: https://code.csdn.net/winlinvip/srs-csdn
[oschina]: http://git.oschina.net/winlinvip/srs.oschina
[srs-dolphin]: https://github.com/ossrs/srs-dolphin
[oryx]: https://github.com/ossrs/go-oryx
[srs-bench]: https://github.com/ossrs/srs-bench
[srs-ngb]: https://github.com/ossrs/srs-ngb
[srs-librtmp]: https://github.com/ossrs/srs-librtmp
[gitlab]: https://gitlab.com/winlinvip/srs-gitlab
[console]: http://ossrs.net:1985/console
[player]: http://ossrs.net/players/srs_player.html
[modules]: https://github.com/ossrs/srs/blob/develop/trunk/modules/readme.txt

[v1_CN_Git]: https://github.com/ossrs/srs/wiki/v1_CN_Git
[v1_EN_Git]: https://github.com/ossrs/srs/wiki/v1_EN_Git
[v1_CN_SampleRTMP]: https://github.com/ossrs/srs/wiki/v1_CN_SampleRTMP
[v1_EN_SampleRTMP]: https://github.com/ossrs/srs/wiki/v1_EN_SampleRTMP
[v3_CN_SampleRTMPCluster]: https://github.com/ossrs/srs/wiki/v3_CN_SampleRTMPCluster
[v3_EN_SampleRTMPCluster]: https://github.com/ossrs/srs/wiki/v3_EN_SampleRTMPCluster
[v2_CN_SampleHLS]: https://github.com/ossrs/srs/wiki/v2_CN_SampleHLS
[v2_EN_SampleHLS]: https://github.com/ossrs/srs/wiki/v2_EN_SampleHLS
[v2_CN_SampleTranscode2HLS]: https://github.com/ossrs/srs/wiki/v2_CN_SampleTranscode2HLS
[v2_EN_SampleTranscode2HLS]: https://github.com/ossrs/srs/wiki/v2_EN_SampleTranscode2HLS
[v2_CN_SampleFFMPEG]: https://github.com/ossrs/srs/wiki/v2_CN_SampleFFMPEG
[v2_EN_SampleFFMPEG]: https://github.com/ossrs/srs/wiki/v2_EN_SampleFFMPEG
[v3_CN_SampleForward]: https://github.com/ossrs/srs/wiki/v3_CN_SampleForward
[v3_EN_SampleForward]: https://github.com/ossrs/srs/wiki/v3_EN_SampleForward
[v3_CN_SampleRealtime]: https://github.com/ossrs/srs/wiki/v3_CN_SampleRealtime
[v3_EN_SampleRealtime]: https://github.com/ossrs/srs/wiki/v3_EN_SampleRealtime
[v1_CN_SampleARM]: https://github.com/ossrs/srs/wiki/v1_CN_SampleARM
[v1_EN_SampleARM]: https://github.com/ossrs/srs/wiki/v1_EN_SampleARM
[v1_CN_SampleIngest]: https://github.com/ossrs/srs/wiki/v1_CN_SampleIngest
[v1_EN_SampleIngest]: https://github.com/ossrs/srs/wiki/v1_EN_SampleIngest
[v2_CN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v2_CN_SampleHTTP
[v2_EN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v2_EN_SampleHTTP
[v1_CN_SampleDemo]: https://github.com/ossrs/srs/wiki/v1_CN_SampleDemo
[v1_EN_SampleDemo]: https://github.com/ossrs/srs/wiki/v1_EN_SampleDemo
[v3_CN_SrsLibrtmp2]: https://github.com/ossrs/srs/wiki/v3_CN_SrsLibrtmp#publish-h264-raw-data
[v3_EN_SrsLibrtmp2]: https://github.com/ossrs/srs/wiki/v3_EN_SrsLibrtmp#publish-h264-raw-data
[v1_CN_Sample]: https://github.com/ossrs/srs/wiki/v1_CN_Sample
[v1_EN_Sample]: https://github.com/ossrs/srs/wiki/v1_EN_Sample
[v1_CN_Product]: https://github.com/ossrs/srs/wiki/v1_CN_Product
[v1_EN_Product]: https://github.com/ossrs/srs/wiki/v1_EN_Product
[v1-wiki-cn]: https://github.com/ossrs/srs/wiki/v1-wiki-cn
[v1-wiki-en]: https://github.com/ossrs/srs/wiki/v1-wiki-en
[v2-wiki-cn]: https://github.com/ossrs/srs/wiki/v2-wiki-cn
[v2-wiki-en]: https://github.com/ossrs/srs/wiki/v2-wiki-en
[v3_CN_Home]: https://github.com/ossrs/srs/wiki/v3_CN_Home
[v3_EN_Home]: https://github.com/ossrs/srs/wiki/v3_EN_Home
[donation0]: http://winlinvip.github.io/srs.release/donation/index.html
[donation1]: http://www.ossrs.net/srs.release/donation/index.html
[donation2]: http://www.ossrs.net/srs.release/donation/paypal.html
[donations]: https://github.com/ossrs/srs/blob/develop/DONATIONS.txt

[v3_CN_Build]: https://github.com/ossrs/srs/wiki/v3_CN_Build
[v3_EN_Build]: https://github.com/ossrs/srs/wiki/v3_EN_Build
[v1_CN_Performance]: https://github.com/ossrs/srs/wiki/v1_CN_Performance
[v1_EN_Performance]: https://github.com/ossrs/srs/wiki/v1_EN_Performance
[v1_CN_DeliveryRTMP]: https://github.com/ossrs/srs/wiki/v1_CN_DeliveryRTMP
[v1_EN_DeliveryRTMP]: https://github.com/ossrs/srs/wiki/v1_EN_DeliveryRTMP
[v3_CN_Edge]: https://github.com/ossrs/srs/wiki/v3_CN_Edge
[v3_EN_Edge]: https://github.com/ossrs/srs/wiki/v3_EN_Edge
[v1_CN_RtmpUrlVhost]: https://github.com/ossrs/srs/wiki/v1_CN_RtmpUrlVhost
[v1_EN_RtmpUrlVhost]: https://github.com/ossrs/srs/wiki/v1_EN_RtmpUrlVhost
[v1_CN_RTMPHandshake]: https://github.com/ossrs/srs/wiki/v1_CN_RTMPHandshake
[v1_EN_RTMPHandshake]: https://github.com/ossrs/srs/wiki/v1_EN_RTMPHandshake
[v2_CN_HTTPServer]: https://github.com/ossrs/srs/wiki/v2_CN_HTTPServer
[v2_EN_HTTPServer]: https://github.com/ossrs/srs/wiki/v2_EN_HTTPServer
[v2_CN_DeliveryHLS]: https://github.com/ossrs/srs/wiki/v2_CN_DeliveryHLS
[v2_EN_DeliveryHLS]: https://github.com/ossrs/srs/wiki/v2_EN_DeliveryHLS
[v2_CN_DeliveryHLS2]: https://github.com/ossrs/srs/wiki/v2_CN_DeliveryHLS#hlsaudioonly
[v2_EN_DeliveryHLS2]: https://github.com/ossrs/srs/wiki/v2_EN_DeliveryHLS#hlsaudioonly
[v1_CN_Reload]: https://github.com/ossrs/srs/wiki/v1_CN_Reload
[v1_EN_Reload]: https://github.com/ossrs/srs/wiki/v1_EN_Reload
[v3_CN_LowLatency2]: https://github.com/ossrs/srs/wiki/v3_CN_LowLatency#gop-cache
[v3_EN_LowLatency2]: https://github.com/ossrs/srs/wiki/v3_EN_LowLatency#gop-cache
[v3_CN_Forward]: https://github.com/ossrs/srs/wiki/v3_CN_Forward
[v3_EN_Forward]: https://github.com/ossrs/srs/wiki/v3_EN_Forward
[v3_CN_FFMPEG]: https://github.com/ossrs/srs/wiki/v3_CN_FFMPEG
[v3_EN_FFMPEG]: https://github.com/ossrs/srs/wiki/v3_EN_FFMPEG
[v3_CN_HTTPCallback]: https://github.com/ossrs/srs/wiki/v3_CN_HTTPCallback
[v3_EN_HTTPCallback]: https://github.com/ossrs/srs/wiki/v3_EN_HTTPCallback
[v1_CN_BandwidthTestTool]: https://github.com/ossrs/srs/wiki/v1_CN_BandwidthTestTool
[v1_EN_BandwidthTestTool]: https://github.com/ossrs/srs/wiki/v1_EN_BandwidthTestTool
[v1_CN_SampleDemo]: https://github.com/ossrs/srs/wiki/v1_CN_SampleDemo
[v1_EN_SampleDemo]: https://github.com/ossrs/srs/wiki/v1_EN_SampleDemo
[v3_CN_SrsLibrtmp]: https://github.com/ossrs/srs/wiki/v3_CN_SrsLibrtmp
[v3_EN_SrsLibrtmp]: https://github.com/ossrs/srs/wiki/v3_EN_SrsLibrtmp
[v1_CN_SrsLinuxArm]: https://github.com/ossrs/srs/wiki/v1_CN_SrsLinuxArm
[v1_EN_SrsLinuxArm]: https://github.com/ossrs/srs/wiki/v1_EN_SrsLinuxArm
[v1_CN_LinuxService]: https://github.com/ossrs/srs/wiki/v1_CN_LinuxService
[v1_EN_LinuxService]: https://github.com/ossrs/srs/wiki/v1_EN_LinuxService
[v3_CN_RTMP-ATC]: https://github.com/ossrs/srs/wiki/v3_CN_RTMP-ATC
[v3_EN_RTMP-ATC]: https://github.com/ossrs/srs/wiki/v3_EN_RTMP-ATC
[v3_CN_HTTPApi]: https://github.com/ossrs/srs/wiki/v3_CN_HTTPApi
[v3_EN_HTTPApi]: https://github.com/ossrs/srs/wiki/v3_EN_HTTPApi
[v1_CN_Ingest]: https://github.com/ossrs/srs/wiki/v1_CN_Ingest
[v1_EN_Ingest]: https://github.com/ossrs/srs/wiki/v1_EN_Ingest
[v3_CN_DVR]: https://github.com/ossrs/srs/wiki/v3_CN_DVR
[v3_EN_DVR]: https://github.com/ossrs/srs/wiki/v3_EN_DVR
[v1_CN_SrsLog]: https://github.com/ossrs/srs/wiki/v1_CN_SrsLog
[v1_EN_SrsLog]: https://github.com/ossrs/srs/wiki/v1_EN_SrsLog
[v3_CN_DRM2]: https://github.com/ossrs/srs/wiki/v3_CN_DRM#tokentraverse
[v3_EN_DRM2]: https://github.com/ossrs/srs/wiki/v3_EN_DRM#tokentraverse
[v2_CN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v2_CN_SampleHTTP
[v2_EN_SampleHTTP]: https://github.com/ossrs/srs/wiki/v2_EN_SampleHTTP
[v2_CN_FlvVodStream]: https://github.com/ossrs/srs/wiki/v2_CN_FlvVodStream
[v2_EN_FlvVodStream]: https://github.com/ossrs/srs/wiki/v2_EN_FlvVodStream
[v3_CN_SrsLibrtmp2]: https://github.com/ossrs/srs/wiki/v3_CN_SrsLibrtmp#publish-h264-raw-data
[v3_EN_SrsLibrtmp2]: https://github.com/ossrs/srs/wiki/v3_EN_SrsLibrtmp#publish-h264-raw-data
[v3_CN_SrsLibrtmp3]: https://github.com/ossrs/srs/wiki/v3_CN_SrsLibrtmp#publish-audio-raw-stream
[v3_EN_SrsLibrtmp3]: https://github.com/ossrs/srs/wiki/v3_EN_SrsLibrtmp#publish-audio-raw-stream
[v2_CN_Security]: https://github.com/ossrs/srs/wiki/v2_CN_Security
[v2_EN_Security]: https://github.com/ossrs/srs/wiki/v2_EN_Security
[v2_CN_DeliveryHttpStream]: https://github.com/ossrs/srs/wiki/v2_CN_DeliveryHttpStream
[v2_EN_DeliveryHttpStream]: https://github.com/ossrs/srs/wiki/v2_EN_DeliveryHttpStream
[v2_CN_DeliveryHDS]: https://github.com/ossrs/srs/wiki/v2_CN_DeliveryHDS
[v2_EN_DeliveryHDS]: https://github.com/ossrs/srs/wiki/v2_EN_DeliveryHDS
[v2_CN_Streamer]: https://github.com/ossrs/srs/wiki/v2_CN_Streamer
[v2_EN_Streamer]: https://github.com/ossrs/srs/wiki/v2_EN_Streamer
[v2_CN_Streamer2]: https://github.com/ossrs/srs/wiki/v2_CN_Streamer#push-http-flv-to-srs
[v2_EN_Streamer2]: https://github.com/ossrs/srs/wiki/v2_EN_Streamer#push-http-flv-to-srs
[v2_CN_SampleHttpFlv]: https://github.com/ossrs/srs/wiki/v2_CN_SampleHttpFlv
[v2_EN_SampleHttpFlv]: https://github.com/ossrs/srs/wiki/v2_EN_SampleHttpFlv
[v3_CN_SampleHttpFlvCluster]: https://github.com/ossrs/srs/wiki/v3_CN_SampleHttpFlvCluster
[v3_EN_SampleHttpFlvCluster]: https://github.com/ossrs/srs/wiki/v3_EN_SampleHttpFlvCluster
[v3_CN_LowLatency]: https://github.com/ossrs/srs/wiki/v3_CN_LowLatency
[v3_EN_LowLatency]: https://github.com/ossrs/srs/wiki/v3_EN_LowLatency
[v3_EN_LowLatency#merged-read]: https://github.com/ossrs/srs/wiki/v3_EN_LowLatency#merged-read
[v1_CN_Performance#performancereport4k]: https://github.com/ossrs/srs/wiki/v1_CN_Performance#performancereport4k
[v3_CN_DRM#tokentraverse]: https://github.com/ossrs/srs/wiki/v3_CN_DRM#tokentraverse
[v1_CN_RaspberryPi]: https://github.com/ossrs/srs/wiki/v1_CN_RaspberryPi
[v3_CN_SrsLibrtmp]: https://github.com/ossrs/srs/wiki/v3_CN_SrsLibrtmp
[v3_CN_Build]: https://github.com/ossrs/srs/wiki/v3_CN_Build
[v3_CN_LowLatency]: https://github.com/ossrs/srs/wiki/v3_CN_LowLatency
[v1_CN_HowToAskQuestion]: https://github.com/ossrs/srs/wiki/v1_CN_HowToAskQuestion
[v3_CN_Build]: https://github.com/ossrs/srs/wiki/v3_CN_Build
[v1_CN_Performance]: https://github.com/ossrs/srs/wiki/v1_CN_Performance
[v1_CN_RaspberryPi]: https://github.com/ossrs/srs/wiki/v1_CN_RaspberryPi
[v3_CN_LowLatency#merged-read]: https://github.com/ossrs/srs/wiki/v3_CN_LowLatency#merged-read
[v1_CN_Product]: https://github.com/ossrs/srs/wiki/v1_CN_Product
[v1_CN_ServerSideScript]: https://github.com/ossrs/srs/wiki/v1_CN_ServerSideScript
[v3_EN_LowLatency#merged-write]: https://github.com/ossrs/srs/wiki/v3_EN_LowLatency#merged-write
[v2_CN_IDE]: https://github.com/ossrs/srs/wiki/v2_CN_IDE
[v3_CN_LowLatency#merged-write]: https://github.com/ossrs/srs/wiki/v3_CN_LowLatency#merged-write
[v3_CN_NgExec]:https://github.com/ossrs/srs/wiki/v3_CN_NgExec
[v3_EN_NgExec]:https://github.com/ossrs/srs/wiki/v3_EN_NgExec

[bug #213]: https://github.com/ossrs/srs/issues/213
[bug #194]: https://github.com/ossrs/srs/issues/194
[bug #182]: https://github.com/ossrs/srs/issues/182
[bug #257]: https://github.com/ossrs/srs/issues/257
[bug #179]: https://github.com/ossrs/srs/issues/179
[bug #224]: https://github.com/ossrs/srs/issues/224
[bug #251]: https://github.com/ossrs/srs/issues/251
[bug #293]: https://github.com/ossrs/srs/issues/293
[bug #250]: https://github.com/ossrs/srs/issues/250
[bug #301]: https://github.com/ossrs/srs/issues/301
[bug #304]: https://github.com/ossrs/srs/issues/304
[bug #133]: https://github.com/ossrs/srs/issues/133
[bug #92]: https://github.com/ossrs/srs/issues/92
[bug #367]: https://github.com/ossrs/srs/issues/367
[bug #471]: https://github.com/ossrs/srs/issues/471
[bug #380]: https://github.com/ossrs/srs/issues/380
[bug #474]: https://github.com/ossrs/srs/issues/474
[bug #484]: https://github.com/ossrs/srs/issues/484
[bug #485]: https://github.com/ossrs/srs/issues/485
[bug #495]: https://github.com/ossrs/srs/issues/495
[bug #497]: https://github.com/ossrs/srs/issues/497
[bug #448]: https://github.com/ossrs/srs/issues/448
[bug #475]: https://github.com/ossrs/srs/issues/475
[bug #458]: https://github.com/ossrs/srs/issues/458
[bug #454]: https://github.com/ossrs/srs/issues/454
[bug #442]: https://github.com/ossrs/srs/issues/442
[bug #169]: https://github.com/ossrs/srs/issues/169
[bug #441]: https://github.com/ossrs/srs/issues/441
[bug #433]: https://github.com/ossrs/srs/issues/433
[bug #425]: https://github.com/ossrs/srs/issues/425
[bug #424]: https://github.com/ossrs/srs/issues/424
[bug #421]: https://github.com/ossrs/srs/issues/421
[bug #435]: https://github.com/ossrs/srs/issues/435
[bug #420]: https://github.com/ossrs/srs/issues/420
[bug #209]: https://github.com/ossrs/srs/issues/209
[bug #409]: https://github.com/ossrs/srs/issues/409
[bug #404]: https://github.com/ossrs/srs/issues/404
[bug #391]: https://github.com/ossrs/srs/issues/391
[bug #397]: https://github.com/ossrs/srs/issues/397
[bug #400]: https://github.com/ossrs/srs/issues/400
[bug #383]: https://github.com/ossrs/srs/issues/383
[bug #381]: https://github.com/ossrs/srs/issues/381
[bug #375]: https://github.com/ossrs/srs/issues/375
[bug #304]: https://github.com/ossrs/srs/issues/304
[bug #372]: https://github.com/ossrs/srs/issues/372
[bug #366]: https://github.com/ossrs/srs/issues/366
[bug #351]: https://github.com/ossrs/srs/issues/351
[bug #155]: https://github.com/ossrs/srs/issues/155
[bug #324]: https://github.com/ossrs/srs/issues/324
[bug #324]: https://github.com/ossrs/srs/issues/324
[bug #328]: https://github.com/ossrs/srs/issues/328
[bug #155]: https://github.com/ossrs/srs/issues/155
[bug #316]: https://github.com/ossrs/srs/issues/316
[bug #310]: https://github.com/ossrs/srs/issues/310
[bug #322]: https://github.com/ossrs/srs/issues/322
[bug #179]: https://github.com/ossrs/srs/issues/179
[bug #304]: https://github.com/ossrs/srs/issues/304
[bug #133]: https://github.com/ossrs/srs/issues/133
[bug #304]: https://github.com/ossrs/srs/issues/304
[bug #304]: https://github.com/ossrs/srs/issues/304
[bug #304]: https://github.com/ossrs/srs/issues/304
[bug #311]: https://github.com/ossrs/srs/issues/311
[bug #310]: https://github.com/ossrs/srs/issues/310
[bug #136]: https://github.com/ossrs/srs/issues/136
[bug #250]: https://github.com/ossrs/srs/issues/250
[bug #301]: https://github.com/ossrs/srs/issues/301
[bug #301]: https://github.com/ossrs/srs/issues/301
[bug #268]: https://github.com/ossrs/srs/issues/268
[bug #151]: https://github.com/ossrs/srs/issues/151
[bug #151]: https://github.com/ossrs/srs/issues/151
[bug #293]: https://github.com/ossrs/srs/issues/293
[bug #293]: https://github.com/ossrs/srs/issues/293
[bug #293]: https://github.com/ossrs/srs/issues/293
[bug #277]: https://github.com/ossrs/srs/issues/277
[bug #277]: https://github.com/ossrs/srs/issues/277
[bug #290]: https://github.com/ossrs/srs/issues/290
[bug #281]: https://github.com/ossrs/srs/issues/281
[bug #274]: https://github.com/ossrs/srs/issues/274
[bug #179]: https://github.com/ossrs/srs/issues/179
[bug #211]: https://github.com/ossrs/srs/issues/211
[bug #207]: https://github.com/ossrs/srs/issues/207
[bug #158]: https://github.com/ossrs/srs/issues/158
[bug #216]: https://github.com/ossrs/srs/issues/216
[bug #263]: https://github.com/ossrs/srs/issues/263
[bug #270]: https://github.com/ossrs/srs/issues/270
[bug #266]: https://github.com/ossrs/srs/issues/266
[bug #267]: https://github.com/ossrs/srs/issues/267
[bug #268]: https://github.com/ossrs/srs/issues/268
[bug #264]: https://github.com/ossrs/srs/issues/264
[bug #264]: https://github.com/ossrs/srs/issues/264
[bug #257]: https://github.com/ossrs/srs/issues/257
[bug #251]: https://github.com/ossrs/srs/issues/251
[bug #251]: https://github.com/ossrs/srs/issues/251
[bug #241]: https://github.com/ossrs/srs/issues/241
[bug #241]: https://github.com/ossrs/srs/issues/241
[bug #241]: https://github.com/ossrs/srs/issues/241
[bug #248]: https://github.com/ossrs/srs/issues/248
[bug #244]: https://github.com/ossrs/srs/issues/244
[bug #237]: https://github.com/ossrs/srs/issues/237
[bug #235]: https://github.com/ossrs/srs/issues/235
[bug #215]: https://github.com/ossrs/srs/issues/215
[bug #212]: https://github.com/ossrs/srs/issues/212
[bug #217]: https://github.com/ossrs/srs/issues/217
[bug #212]: https://github.com/ossrs/srs/issues/212
[bug #213]: https://github.com/ossrs/srs/issues/213
[bug #204]: https://github.com/ossrs/srs/issues/204
[bug #203]: https://github.com/ossrs/srs/issues/203
[bug #202]: https://github.com/ossrs/srs/issues/202
[bug #200]: https://github.com/ossrs/srs/issues/200
[bug #194]: https://github.com/ossrs/srs/issues/194
[bug #194]: https://github.com/ossrs/srs/issues/194
[bug #195]: https://github.com/ossrs/srs/issues/195
[bug #191]: https://github.com/ossrs/srs/issues/191
[bug #66]: https://github.com/ossrs/srs/issues/66
[bug #185]: https://github.com/ossrs/srs/issues/185
[bug #186]: https://github.com/ossrs/srs/issues/186
[bug #184]: https://github.com/ossrs/srs/issues/184
[bug #151]: https://github.com/ossrs/srs/issues/151
[bug #162]: https://github.com/ossrs/srs/issues/162
[bug #180]: https://github.com/ossrs/srs/issues/180
[bug #177]: https://github.com/ossrs/srs/issues/177
[bug #167]: https://github.com/ossrs/srs/issues/167
[bug #150]: https://github.com/ossrs/srs/issues/150
[bug #165]: https://github.com/ossrs/srs/issues/165
[bug #160]: https://github.com/ossrs/srs/issues/160
[bug #155]: https://github.com/ossrs/srs/issues/155
[bug #148]: https://github.com/ossrs/srs/issues/148
[bug #147]: https://github.com/ossrs/srs/issues/147
[bug #79]: https://github.com/ossrs/srs/issues/79
[bug #57]: https://github.com/ossrs/srs/issues/57
[bug #85]: https://github.com/ossrs/srs/issues/85
[bug #145]: https://github.com/ossrs/srs/issues/145
[bug #143]: https://github.com/ossrs/srs/issues/143
[bug #138]: https://github.com/ossrs/srs/issues/138
[bug #142]: https://github.com/ossrs/srs/issues/142
[bug #141]: https://github.com/ossrs/srs/issues/141
[bug #124]: https://github.com/ossrs/srs/issues/124
[bug #121]: https://github.com/ossrs/srs/issues/121
[bug #119]: https://github.com/ossrs/srs/issues/119
[bug #81]: https://github.com/ossrs/srs/issues/81
[bug #103]: https://github.com/ossrs/srs/issues/103
[bug #111]: https://github.com/ossrs/srs/issues/111
[bug #110]: https://github.com/ossrs/srs/issues/110
[bug #109]: https://github.com/ossrs/srs/issues/109
[bug #108]: https://github.com/ossrs/srs/issues/108
[bug #98]: https://github.com/ossrs/srs/issues/98
[bug #87]: https://github.com/ossrs/srs/issues/87
[bug #84]: https://github.com/ossrs/srs/issues/84
[bug #89]: https://github.com/ossrs/srs/issues/89
[bug #76]: https://github.com/ossrs/srs/issues/76
[bug #78]: https://github.com/ossrs/srs/issues/78
[bug #74]: https://github.com/ossrs/srs/issues/74
[bug #72]: https://github.com/ossrs/srs/issues/72
[bug #67]: https://github.com/ossrs/srs/issues/67
[bug #64]: https://github.com/ossrs/srs/issues/64
[bug #36]: https://github.com/ossrs/srs/issues/36
[bug #60]: https://github.com/ossrs/srs/issues/60
[bug #59]: https://github.com/ossrs/srs/issues/59
[bug #50]: https://github.com/ossrs/srs/issues/50
[bug #34]: https://github.com/ossrs/srs/issues/34
[bug #257-c0]: https://github.com/ossrs/srs/issues/257#issuecomment-66864413
[bug #110]: https://github.com/ossrs/srs/issues/110
[bug #109]: https://github.com/ossrs/srs/issues/109
[bug #108]: https://github.com/ossrs/srs/issues/108
[bug #104]: https://github.com/ossrs/srs/issues/104
[bug #98]: https://github.com/ossrs/srs/issues/98
[bug #87]: https://github.com/ossrs/srs/issues/87
[bug #84]: https://github.com/ossrs/srs/issues/84
[bug #89]: https://github.com/ossrs/srs/issues/89
[bug #76]: https://github.com/ossrs/srs/issues/76
[bug #78]: https://github.com/ossrs/srs/issues/78
[bug #74]: https://github.com/ossrs/srs/issues/74
[bug #72]: https://github.com/ossrs/srs/issues/72
[bug #67]: https://github.com/ossrs/srs/issues/67
[bug #64]: https://github.com/ossrs/srs/issues/64
[bug #36]: https://github.com/ossrs/srs/issues/36
[bug #60]: https://github.com/ossrs/srs/issues/60
[bug #59]: https://github.com/ossrs/srs/issues/59
[bug #50]: https://github.com/ossrs/srs/issues/50
[bug #34]: https://github.com/ossrs/srs/issues/34
[bug #367]: https://github.com/ossrs/srs/issues/367
[bug #319]: https://github.com/ossrs/srs/issues/319
[bug #367]: https://github.com/ossrs/srs/issues/367
[bug #459]: https://github.com/ossrs/srs/issues/459
[bug #470]: https://github.com/ossrs/srs/issues/470
[bug #319]: https://github.com/ossrs/srs/issues/319
[bug #467]: https://github.com/ossrs/srs/issues/467
[bug #464]: https://github.com/ossrs/srs/issues/464
[bug #465]: https://github.com/ossrs/srs/issues/465
[bug #299]: https://github.com/ossrs/srs/issues/299
[bug #92]: https://github.com/ossrs/srs/issues/92
[bug #299]: https://github.com/ossrs/srs/issues/299
[bug #466]: https://github.com/ossrs/srs/issues/466
[bug #468]: https://github.com/ossrs/srs/issues/468
[bug #502]: https://github.com/ossrs/srs/issues/502
[bug #467]: https://github.com/ossrs/srs/issues/467
[bug #512]: https://github.com/ossrs/srs/issues/512
[bug #515]: https://github.com/ossrs/srs/issues/515
[bug #511]: https://github.com/ossrs/srs/issues/511
[bug #518]: https://github.com/ossrs/srs/issues/518
[bug #541]: https://github.com/ossrs/srs/issues/541
[bug #546]: https://github.com/ossrs/srs/issues/546
[bug #418]: https://github.com/ossrs/srs/issues/418
[bug #509]: https://github.com/ossrs/srs/issues/509
[bug #511]: https://github.com/ossrs/srs/issues/511
[bug #717]: https://github.com/ossrs/srs/issues/717
[bug #691]: https://github.com/ossrs/srs/issues/691
[bug #711]: https://github.com/ossrs/srs/issues/711
[bug #640]: https://github.com/ossrs/srs/issues/640
[bug #661]: https://github.com/ossrs/srs/issues/661
[bug #666]: https://github.com/ossrs/srs/issues/666
[bug #654]: https://github.com/ossrs/srs/issues/654
[bug #713]: https://github.com/ossrs/srs/issues/713
[bug #513]: https://github.com/ossrs/srs/issues/513
[bug #730]: https://github.com/ossrs/srs/issues/730
[bug #635]: https://github.com/ossrs/srs/issues/635
[bug #736]: https://github.com/ossrs/srs/issues/736
[bug #588]: https://github.com/ossrs/srs/issues/588
[bug #740]: https://github.com/ossrs/srs/issues/740
[bug #749]: https://github.com/ossrs/srs/issues/749
[bug #750]: https://github.com/ossrs/srs/issues/750
[bug #752]: https://github.com/ossrs/srs/issues/752
[bug #503]: https://github.com/ossrs/srs/issues/503
[bug #xxxxxxxxxx]: https://github.com/ossrs/srs/issues/xxxxxxxxxx

[bug #735]: https://github.com/ossrs/srs/issues/735
[bug #742]: https://github.com/ossrs/srs/issues/742
[bug #738]: https://github.com/ossrs/srs/issues/738
[bug #xxxxxxxxxxxxx]: https://github.com/ossrs/srs/issues/xxxxxxxxxxxxx

[exo #828]: https://github.com/google/ExoPlayer/pull/828

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


[contact]: https://github.com/ossrs/srs/wiki/v1_CN_Contact
[more0]: http://winlinvip.github.io/srs.release/releases/
[more1]: http://www.ossrs.net/srs.release/releases/

[srs_CN]: https://github.com/ossrs/srs/wiki/v3_CN_Home
[srs_EN]: https://github.com/ossrs/srs/wiki/v3_EN_Home
[branch1]: https://github.com/ossrs/srs/tree/1.0release
[branch2]: https://github.com/ossrs/srs/tree/2.0release
[release2]: https://github.com/ossrs/srs/wiki/v1_CN_Product#release20
[release3]: https://github.com/ossrs/srs/wiki/v1_CN_Product#release30
[centos0]: http://winlinvip.github.io/srs.release/releases/files/SRS-CentOS6-x86_64-2.0.230.zip
[centos1]: http://www.ossrs.net/srs.release/releases/files/SRS-CentOS6-x86_64-2.0.230.zip

