# SRS(Simple Realtime Server)

![](http://ossrs.net/gif/v1/sls.gif?site=github.com&path=/srs/develop)
[![](https://circleci.com/gh/ossrs/srs/tree/develop.svg?style=svg&circle-token=1ef1d5b5b0cde6c8c282ed856a18199f9e8f85a9)](https://circleci.com/gh/ossrs/srs/tree/develop)
[![](https://codecov.io/gh/ossrs/srs/branch/develop/graph/badge.svg)](https://codecov.io/gh/ossrs/srs/branch/develop)
[![](https://cloud.githubusercontent.com/assets/2777660/22814959/c51cbe72-ef92-11e6-81cc-32b657b285d5.png)](https://github.com/ossrs/srs/wiki/v1_CN_Contact#wechat)

SRS/4.0，[Leo][release4]，是一个简单高效的实时视频服务器，支持RTMP/WebRTC/HLS/HTTP-FLV/SRT/GB28181。

SRS is a simple, high efficiency and realtime video server, supports RTMP/WebRTC/HLS/HTTP-FLV/SRT/GB28181.

SRS is licenced under [MIT][LICENSE], but some depended libraries are distributed using their [own licenses][LicenseMixing].

<a name="product"></a>
<a name="usage-docker"></a>
## Usage

Run SRS by [docker][docker-srs4], images is [here](https://hub.docker.com/r/ossrs/srs/tags) or [there](https://cr.console.aliyun.com/repository/cn-hangzhou/ossrs/srs/images), 
please set the CANDIDATE ([CN][v4_CN_WebRTC#config-candidate],[EN][v4_EN_WebRTC#config-candidate]) if WebRTC enabled:

```bash
docker run --rm -it -p 1935:1935 -p 1985:1985 -p 8080:8080 \
    --env CANDIDATE=$(ifconfig en0 inet| grep 'inet '|awk '{print $2}') -p 8000:8000/udp \
    ossrs/srs:v4.0.117 ./objs/srs -c conf/srs.conf
```

<a name="usage-source"></a>
Or build SRS from source(or [mirrors](#mirrors)), by CentOS7(or Linux([CN][v4_CN_Build],[EN][v4_EN_Build])):

```
git clone -b develop https://gitee.com/ossrs/srs.git &&
cd srs/trunk && ./configure && make && ./objs/srs -c conf/srs.conf
```

Open [http://localhost:8080/](http://localhost:8080/) to check it, then publish 
[stream](https://github.com/ossrs/srs/blob/3.0release/trunk/doc/source.flv) by FFmpeg. 
It's also able to [publish by H5](http://localhost:8080/players/rtc_publisher.html?autostart=true) if WebRTC is enabled:

```bash
docker run --rm -it --network=host ossrs/srs:encoder \
  ffmpeg -re -i ./doc/source.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

Play the following streams by [players](https://ossrs.net):

* RTMP (by [VLC](https://www.videolan.org/)): rtmp://localhost/live/livestream
* H5(HTTP-FLV): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)
* H5(HLS): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8&port=8080&schema=http)
* H5(WebRTC): [webrtc://localhost/live/livestream](http://localhost:8080/players/rtc_player.html?autostart=true)

<a name="srs-40-wiki"></a>
<a name="wiki"></a>

From here, please read wikis:

* [SRS 4.0 English Wiki][v4_EN_Home], please read Wiki first.
* [SRS 4.0 中文Wiki][v4_CN_Home]，不读Wiki一定扑街，不读文档请不要提Issue，不读文档请不要提问题，任何文档中明确说过的疑问都不会解答。

Fast index for Wikis:

* How to deliver RTMP streaming?([CN][v4_CN_SampleRTMP], [EN][v4_EN_SampleRTMP])
* How to build RTMP Edge-Cluster?([CN][v4_CN_SampleRTMPCluster], [EN][v4_EN_SampleRTMPCluster])
* How to build RTMP Origin-Cluster?([CN][v4_CN_SampleOriginCluster], [EN][v4_EN_SampleOriginCluster])
* How to deliver HTTP-FLV streaming?([CN][v4_CN_SampleHttpFlv], [EN][v4_EN_SampleHttpFlv])
* How to deliver HLS streaming?([CN][v4_CN_SampleHLS], [EN][v4_EN_SampleHLS])
* How to deliver low-latency streaming?([CN][v4_CN_SampleRealtime], [EN][v4_EN_SampleRealtime])
* How to use WebRTC? ([CN][v4_CN_WebRTC], [EN][v4_EN_WebRTC])

Other important wiki:

* Usage: How to publish GB28181 to SRS? [#1500](https://github.com/ossrs/srs/issues/1500#issuecomment-606695679)
* Usage: How to delivery DASH(Experimental)?([CN][v4_CN_SampleDASH], [EN][v4_EN_SampleDASH])
* Usage: How to transode RTMP stream by FFMPEG?([CN][v4_CN_SampleFFMPEG], [EN][v4_EN_SampleFFMPEG])
* Usage: How to delivery HTTP FLV Live Streaming Cluster?([CN][v4_CN_SampleHttpFlvCluster], [EN][v4_EN_SampleHttpFlvCluster])
* Usage: How to ingest file/stream/device to RTMP?([CN][v4_CN_SampleIngest], [EN][v4_EN_SampleIngest])
* Usage: How to forward stream to other servers?([CN][v4_CN_SampleForward], [EN][v4_EN_SampleForward])
* Usage: How to improve edge performance for multiple CPUs? ([CN][v4_CN_REUSEPORT], [EN][v4_EN_REUSEPORT])
* Usage: How to file a bug or contact us? ([CN][v4_CN_Contact], [EN][v4_EN_Contact])

## Ports

The ports used by SRS, kernel services:

* tcp://1935, for RTMP live streaming server([CN][v4_CN_DeliveryRTMP],[EN][v4_EN_DeliveryRTMP]).
* tcp://1985, HTTP API server, for HTTP-API([CN][v4_CN_HTTPApi], [EN][v4_EN_HTTPApi]), WebRTC([CN][v4_CN_WebRTC], [EN][v4_EN_WebRTC]), etc.
* tcp://8080, HTTP live streaming server, HTTP-FLV([CN][v4_CN_SampleHttpFlv], [EN][v4_EN_SampleHttpFlv]), HLS([CN][v4_CN_SampleHLS], [EN][v4_EN_SampleHLS]) as such.
* udp://8000, WebRTC Media([CN][v4_CN_WebRTC], [EN][v4_EN_WebRTC]) server.

For optional HTTPS services, which might be provided by other web servers:

* tcp://8088, HTTPS live streaming server.
* tcp://1990, HTTPS API server.

For optional stream caster services, to push streams to SRS:

* udp://8935, Stream Caster: [Push MPEGTS over UDP](https://github.com/ossrs/srs/wiki/v4_CN_Streamer#push-mpeg-ts-over-udp) server.
* tcp://554, Stream Caster: [Push RTSP](https://github.com/ossrs/srs/wiki/v4_CN_Streamer#push-rtsp-to-srs) server.
* tcp://8936, Stream Caster: [Push HTTP-FLV](https://github.com/ossrs/srs/wiki/v4_CN_Streamer#push-http-flv-to-srs) server.
* tcp://5060, Stream Caster: [Push GB28181 SIP](https://github.com/ossrs/srs/issues/1500#issuecomment-606695679) server.
* udp://9000, Stream Caster: [Push GB28181 Media(bundle)](https://github.com/ossrs/srs/issues/1500#issuecomment-606695679) server.
* udp://58200-58300, Stream Caster: [Push GB28181 Media(no-bundle)](https://github.com/ossrs/srs/issues/1500#issuecomment-606695679) server.
* udp://10080, Stream Caster: [Push SRT Media](https://github.com/ossrs/srs/issues/1147#issuecomment-577469119) server.
  
For external services to work with SRS:

* udp://1989, [WebRTC Signaling](https://github.com/ossrs/signaling#usage) server.

## Features

- [x] Using coroutine by ST, it's really simple and stupid enough.
- [x] Support cluster which consists of origin ([CN][v4_CN_DeliveryRTMP],[EN][v4_EN_DeliveryRTMP]) and edge([CN][v4_CN_Edge], [EN][v4_EN_Edge]) server and uses RTMP as default transport protocol.
- [x] Origin server supports remuxing RTMP to HTTP-FLV([CN][v4_CN_SampleHttpFlv], [EN][v4_EN_SampleHttpFlv]) and HLS([CN][v4_CN_DeliveryHLS], [EN][v4_EN_DeliveryHLS]).
- [x] Edge server supports remuxing RTMP to HTTP-FLV([CN][v4_CN_SampleHttpFlv], [EN][v4_EN_SampleHttpFlv]). As for HLS([CN][v4_CN_DeliveryHLS], [EN][v4_EN_DeliveryHLS]) edge server, recomment to use HTTP edge server, such as [NGINX](http://nginx.org/).
- [x] Support HLS with audio-only([CN][v4_CN_DeliveryHLS2], [EN][v4_EN_DeliveryHLS2]), which need to build the timestamp from AAC samples, so we enhanced it please read [#547][bug #547].
- [x] Support HLS with mp3(h.264+mp3) audio codec, please read [bug #301][bug #301].
- [x] Support transmux RTMP to HTTP-FLV/MP3/AAC/TS, please read wiki([CN][v4_CN_DeliveryHttpStream], [EN][v4_CN_DeliveryHttpStream]).
- [x] Support ingesting([CN][v4_CN_Ingest], [EN][v4_EN_Ingest]) other protocols to SRS by FFMPEG.
- [x] Support RTMP long time(>4.6hours) publishing/playing, with the timestamp corrected.
- [x] Support native HTTP server([CN][v4_CN_SampleHTTP], [EN][v4_EN_SampleHTTP]) for http api and http live streaming.
- [x] Support HTTP CORS for js in http api and http live streaming.
- [x] Support HTTP API([CN][v4_CN_HTTPApi], [EN][v4_EN_HTTPApi]) for system management.
- [x] Support HTTP callback([CN][v4_CN_HTTPCallback], [EN][v4_EN_HTTPCallback]) for authentication and integration.
- [x] Support DVR([CN][v4_CN_DVR], [EN][v4_EN_DVR]) to record live streaming to FLV file.
- [x] Support DVR control module like NGINX-RTMP, please read [#459][bug #459].
- [x] Support EXEC like NGINX-RTMP, please read [bug #367][bug #367].
- [x] Support security strategy including allow/deny publish/play IP([CN][v4_CN_Security], [EN][v4_EN_Security]).
- [x] Support low latency(0.1s+) transport model, please read [bug #257][bug #257].
- [x] Support gop-cache([CN][v4_CN_LowLatency2], [EN][v4_EN_LowLatency2]) for player fast startup.
- [x] Support Vhost([CN][v4_CN_RtmpUrlVhost], [EN][v4_EN_RtmpUrlVhost]) and \_\_defaultVhost\_\_.
- [x] Support reloading([CN][v4_CN_Reload], [EN][v4_EN_Reload]) to apply changes of config.
- [x] Support listening at multiple ports.
- [x] Support forwarding([CN][v4_CN_Forward], [EN][v4_EN_Forward]) to other RTMP servers.
- [x] Support transcoding([CN][v4_CN_FFMPEG], [EN][v4_EN_FFMPEG]) by FFMPEG.
- [x] All wikis are writen in [Chinese][v4_CN_Home] and [English][v4_EN_Home]. 
- [x] Enhanced json, replace NXJSON(LGPL) with json-parser(BSD), read [#904][bug #904].
- [x] Support valgrind and latest ARM by patching ST, read [ST#1](https://github.com/ossrs/state-threads/issues/1) and [ST#2](https://github.com/ossrs/state-threads/issues/2).
- [x] Support traceable and session-based log([CN][v4_CN_SrsLog], [EN][v4_EN_SrsLog]).
- [x] High performance([CN][v4_CN_Performance], [EN][v4_EN_Performance]) RTMP/HTTP-FLV, 6000+ connections.
- [x] Enhanced complex error code with description and stack, read [#913][bug #913].
- [x] Enhanced RTMP url  which supports vhost in stream, read [#1059][bug #1059].
- [x] Support origin cluster, please read [#464][bug #464], [RTMP 302][bug #92].
- [x] Support listen at IPv4 and IPv6, read [#460][bug #460].
- [x] Improve test coverage for core/kernel/protocol/service.
- [x] Support docker by [srs-docker](https://github.com/ossrs/srs-docker).
- [x] Support multiple processes by ReusePort([CN][v4_CN_REUSEPORT], [EN][v4_EN_REUSEPORT]), [#775][bug #775].
- [x] Support a simple [mgmt console][console], please read [srs-ngb][srs-ngb].
- [x] [Experimental] Support playing stream by WebRTC, [#307][bug #307].
- [x] [Experimental] Support publishing stream by WebRTC, [#307][bug #307].
- [x] [Experimental] Support mux RTP/RTCP/DTLS/SRTP on one port for WebRTC, [#307][bug #307].
- [x] [Experimental] Support client address changing for WebRTC, [#307][bug #307].
- [x] [Experimental] Support transcode RTMP/AAC to WebRTC/Opus, [#307][bug #307].
- [x] [Experimental] Support AV1 codec for WebRTC, [#2324][bug #2324].
- [x] [Experimental] Enhance HTTP Stream Server for HTTP-FLV, HTTPS, HLS etc. [#1657][bug #1657].
- [x] [Experimental] Support push stream by GB28181, [#1500][bug #1500].
- [x] [Experimental] Support DVR in MP4 format, read [#738][bug #738].
- [x] [Experimental] Support MPEG-DASH, the future live streaming protocol, read [#299][bug #299].
- [x] [Experimental] Support pushing MPEG-TS over UDP, please read [bug #250][bug #250].
- [x] [Experimental] Support pushing FLV over HTTP POST, please read wiki([CN][v4_CN_Streamer2], [EN][v4_EN_Streamer2]).
- [x] [Experimental] Support HTTP RAW API, please read [#459][bug #459], [#470][bug #470], [#319][bug #319].
- [x] [Experimental] Support SRT server, read [#1147][bug #1147].
- [x] [Deprecated] Support pushing RTSP, please read [bug #2304][bug #2304].
- [x] [Deprecated] Support Adobe HDS(f4m), please read wiki([CN][v4_CN_DeliveryHDS], [EN][v4_EN_DeliveryHDS]) and [#1535][bug #1535].
- [x] [Deprecated] Support bandwidth testing, please read [#1535][bug #1535].
- [x] [Deprecated] Support Adobe FMS/AMS token traverse([CN][v4_CN_DRM2], [EN][v4_EN_DRM2]) authentication, please read [#1535][bug #1535].
- [x] [Removed] Support RTMP client library: [srs-librtmp](https://github.com/ossrs/srs-librtmp).
- [ ] Enhanced forwarding with vhost and variables, [#1342][bug #1342].
- [ ] Support DVR to Cloud Storage, [#1193][bug #1193].
- [ ] Support transmux RTC to RTMP, [#2093][bug #2093].
- [ ] Support H.265 over RTMP and HLS, [#465][bug #465].
- [ ] Support IETF-QUIC for WebRTC Cluster, [#2091][bug #2091].
- [ ] Improve RTC performance to 5K by multiple threading, [#2188][bug #2188].
- [ ] Support source cleanup for idle streams, [#413][bug #413].
- [ ] Support change user to run SRS, [#1111][bug #1111].
- [ ] Support HLS variant, [#463][bug #463].

> Remark: About the milestone and product plan, please read ([CN][v4_CN_Product], [EN][v4_EN_Product]) wiki.

<a name="history"></a>
<a name="changes"></a>
<a name="change-logs"></a>

## Changelog

Please read [CHANGELOG](CHANGELOG.md#changelog).

## Releases

* 2021-04-28, [Release v3.0-r5][r3.0r5], 3.0 release5, 3.0.161, 122750 lines.
* 2021-04-24, [Release v3.0-r4][r3.0r4], 3.0 release4, 3.0.160, 122750 lines.
* 2021-01-02, [Release v3.0-r3][r3.0r3], 3.0 release3, 3.0.156, 122736 lines.
* 2020-10-31, [Release v3.0-r2][r3.0r2], 3.0 release2, 3.0.153, 122663 lines.
* 2020-10-10, [Release v3.0-r1][r3.0r1], 3.0 release1, 3.0.144, 122674 lines.
* 2020-06-27, [Release v3.0-r0][r3.0r0], 3.0 release0, 3.0.141, 122674 lines.
* 2020-03-29, [Release v3.0-b3][r3.0b4], 3.0 beta4, 3.0.139, 122674 lines.
* 2020-03-18, [Release v3.0-b3][r3.0b3], 3.0 beta3, 3.0.134, 122509 lines.
* 2020-03-05, [Release v3.0-b2][r3.0b2], 3.0 beta2, 3.0.123, 122170 lines.
* 2020-02-14, [Release v3.0-b1][r3.0b1], 3.0 beta1, 3.0.117, 121964 lines.
* 2020-02-02, [Release v3.0-b0][r3.0b0], 3.0 beta0, 3.0.112, 121709 lines.
* 2020-01-21, [Release v3.0-a9][r3.0a9], 3.0 alpha9, 3.0.105, 121577 lines.
* 2020-01-10, [Release v3.0-a8][r3.0a8], 3.0 alpha8, 3.0.97, 121555 lines.
* 2019-12-29, [Release v3.0-a7][r3.0a7], 3.0 alpha7, 3.0.90, 116356 lines.
* 2019-12-26, [Release v3.0-a6][r3.0a6], 3.0 alpha6, 3.0.85, 116056 lines.
* 2019-12-19, [Release v3.0-a5][r3.0a5], 3.0 alpha5, 3.0.75, 115362 lines.
* 2019-12-13, [Release v3.0-a4][r3.0a4], 3.0 alpha4, 3.0.71, 112928 lines.
* 2019-11-30, [Release v3.0-a3][r3.0a3], 3.0 alpha3, 3.0.67, 110864 lines.
* 2019-11-30, [Release v3.0-a2][r3.0a2], 3.0 alpha2, 3.0.66, 110831 lines.
* 2019-10-07, [Release v3.0-a1][r3.0a1], 3.0 alpha1, 3.0.60, 107962 lines.
* 2019-10-04, [Release v3.0-a0][r3.0a0], 3.0 alpha0, 3.0.56, 107946 lines.
* 2017-03-03, [Release v2.0-r0][r2.0r0], 2.0 release0, 2.0.234, 86373 lines.
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
* 2014-06-27, [Release v0.9.5][r1.0a5], refine perf 3k+ clients, edge token traverse, 30days online. 41573 lines.
* 2014-05-28, [Release v0.9.4][r1.0a4], support heartbeat, tracable log, fix mem leak and bugs. 39200 lines.
* 2014-05-18, [Release v0.9.3][r1.0a3], support mips, fms origin, json(http-api). 37594 lines.
* 2014-04-28, [Release v0.9.2][r1.0a2], support [dvr][v4_CN_DVR], android, [edge][v4_CN_Edge]. 35255 lines.
* 2014-04-07, [Release v0.9.1][r1.0a0], support [arm][v4_CN_SrsLinuxArm], [init.d][v4_CN_LinuxService], http [server][v4_CN_HTTPServer]/[api][v4_CN_HTTPApi], [ingest][v4_CN_SampleIngest]. 30000 lines.
* 2013-12-25, [Release v0.9.0][r0.9], support bandwidth test, player/encoder/chat [demos][v4_CN_SampleDemo]. 20926 lines.
* 2013-12-08, [Release v0.8.0][r0.8], support [http hooks callback][v4_CN_HTTPCallback], update [SB][srs-bench]. 19186 lines.
* 2013-12-03, [Release v0.7.0][r0.7], support [live stream transcoding][v4_CN_FFMPEG]. 17605 lines.
* 2013-11-29, [Release v0.6.0][r0.6], support [forward][v4_CN_Forward] stream to origin/edge. 16094 lines.
* 2013-11-26, [Release v0.5.0][r0.5], support [HLS(m3u8)][v4_CN_DeliveryHLS], fragment and window. 14449 lines.
* 2013-11-10, [Release v0.4.0][r0.4], support [reload][v4_CN_Reload] config, pause, longtime publish/play. 12500 lines.
* 2013-11-04, [Release v0.3.0][r0.3], support [vhost][v4_CN_RtmpUrlVhost], refer, gop cache, listen multiple ports. 11773 lines.
* 2013-10-25, [Release v0.2.0][r0.2], support [rtmp][v4_CN_RTMPHandshake] flash publish, h264, time jitter correct. 10125 lines.
* 2013-10-23, [Release v0.1.0][r0.1], support [rtmp FMLE/FFMPEG publish][v4_CN_DeliveryRTMP], vp6. 8287 lines.
* 2013-10-17, Created.

## Compare

Comparing with other media servers, SRS is much better and stronger, for details please 
read Product([CN][v4_CN_Compare]/[EN][v4_EN_Compare]).

## Performance

The performance benchmark data and corelative commits are listed here.

* See also: [Performance for x86/x64 Test Guide][v4_CN_Performance].
* See also: [Performance for RaspberryPi][v4_CN_RaspberryPi].
* For multiple processes performance, read [#775: REUSEPORT][bug #775] or OriginCluster([CN][v4_EN_OriginCluster]/[EN][v4_EN_OriginCluster]) or [go-oryx][oryx].
* For RTC benchmark, please use [srs-bench](https://github.com/ossrs/srs-bench/tree/feature/rtc#usage).

<a name="play-rtmp-benchmark"></a>
**Play RTMP benchmark**

The data for playing RTMP was benchmarked by [SB][srs-bench]:


|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2014-12-07  |   2.0.67  |   10k(10000)  |   players     |   95%     |   656MB   |   [code][p12] |
|   2014-12-05  |   2.0.57  |   9.0k(9000)  |   players     |   90%     |   468MB   |   [code][p11] |
|   2014-12-05  |   2.0.55  |   8.0k(8000)  |   players     |   89%     |   360MB   |   [code][p10] |
|   2014-11-22  |   2.0.30  |   7.5k(7500)  |   players     |   87%     |   320MB   |   [code][p9]  |
|   2014-11-13  |   2.0.15  |   6.0k(6000)  |   players     |   82%     |   203MB   |   [code][p8]  |
|   2014-11-12  |   2.0.14  |   3.5k(3500)  |   players     |   95%     |   78MB    |   [code][p7]  |
|   2014-11-12  |   2.0.14  |   2.7k(2700)  |   players     |   69%     |   59MB    |   -           |
|   2014-11-11  |   2.0.12  |   2.7k(2700)  |   players     |   85%     |   66MB    |   -           |
|   2014-11-11  |   1.0.5   |   2.7k(2700)  |   players     |   85%     |   66MB    |   -           |
|   2014-07-12  |   0.9.156 |   2.7k(2700)  |   players     |   89%     |   61MB    |   [code][p6]  |
|   2014-07-12  |   0.9.156 |   1.8k(1800)  |   players     |   68%     |   38MB    |   -           |
|   2013-11-28  |   0.5.0   |   1.8k(1800)  |   players     |   90%     |   41M     |   -           |

| Update     |    SFU           |  Clients |     Type      |    CPU    |  Memory   | Threads | VM   |
| ---------- | ---------------- | -------- | ------------- | --------- | --------  | ------- | ---- |
| 2021-05-11 | SRS/v4.0.105     | 4000     |   players     |   ~94% x1 |   419MB   | 1       | G5 8CPU |
| 2021-05-11 | NginxRTMP/v1.2.1 | 2400     |   players     |   ~92% x1 |   173MB   | 1       | G5 8CPU |

> Note: CentOS7, 600Kbps, [ECS/G5-2.5GHZ(SkyLake)](https://help.aliyun.com/document_detail/25378.html),
> [SRS/v4.0.105](https://github.com/ossrs/srs/commit/2ad24b2313e88a85801deaea370204f225555939),
> [NginxRTMP/v1.2.1](https://github.com/arut/nginx-rtmp-module/releases/tag/v1.2.1).

<a name="publish-rtmp-benchmark"></a>
**Publish RTMP benchmark**

The data for publishing RTMP was benchmarked by [SB][srs-bench]:

|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2014-12-04  |   2.0.52  |   4.0k(4000)  |   publishers  |   80%     |   331MB   |   [code][p5]  |
|   2014-12-04  |   2.0.51  |   2.5k(2500)  |   publishers  |   91%     |   259MB   |   [code][p4]  |
|   2014-12-04  |   2.0.49  |   2.5k(2500)  |   publishers  |   95%     |   404MB   |   [code][p3]  |
|   2014-12-04  |   2.0.49  |   1.4k(1400)  |   publishers  |   68%     |   144MB   |   -           |
|   2014-12-03  |   2.0.48  |   1.4k(1400)  |   publishers  |   95%     |   140MB   |   [code][p2]  |
|   2014-12-03  |   2.0.47  |   1.4k(1400)  |   publishers  |   95%     |   140MB   |   -           |
|   2014-12-03  |   2.0.47  |   1.2k(1200)  |   publishers  |   84%     |   76MB    |   [code][p1]  |
|   2014-12-03  |   2.0.12  |   1.2k(1200)  |   publishers  |   96%     |   43MB    |   -           |
|   2014-12-03  |   1.0.10  |   1.2k(1200)  |   publishers  |   96%     |   43MB    |   -           |

| Update     |    SFU           |  Clients |     Type      |    CPU    |  Memory   | Threads | VM   |
| ---------- | ---------------- | -------- | ------------- | --------- | --------  | ------- | ---- |
| 2021-05-11 | SRS/v4.0.105     | 2300     |   publishers  |   ~89% x1 |   1.1GB   | 1       | G5 8CPU |
| 2021-05-11 | NginxRTMP/v1.2.1 | 1300     |   publishers  |   ~84% x1 |   198MB   | 1       | G5 8CPU |

> Note: CentOS7, 600Kbps, [ECS/G5-2.5GHZ(SkyLake)](https://help.aliyun.com/document_detail/25378.html),
> [SRS/v4.0.105](https://github.com/ossrs/srs/commit/2ad24b2313e88a85801deaea370204f225555939),
> [NginxRTMP/v1.2.1](https://github.com/arut/nginx-rtmp-module/releases/tag/v1.2.1).

<a name="play-http-flv-benchmark"></a>
**Play HTTP FLV benchmark**

The data for playing HTTP FLV was benchmarked by [SB][srs-bench]:


|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2014-05-25  |   2.0.171 |   6.0k(6000)  |   players     |   84%     |   297MB   |   [code][p20] |
|   2014-05-24  |   2.0.170 |   3.0k(3000)  |   players     |   89%     |   96MB    |   [code][p19] |
|   2014-05-24  |   2.0.169 |   3.0k(3000)  |   players     |   94%     |   188MB   |   [code][p18] |
|   2014-05-24  |   2.0.168 |   2.3k(2300)  |   players     |   92%     |   276MB   |   [code][p17] |
|   2014-05-24  |   2.0.167 |   1.0k(1000)  |   players     |   82%     |   86MB    |   -           |

<a name="rtc-benchmark"></a>
**RTC benchmark**

The RTC benchmark data, by [srs-bench](https://github.com/ossrs/srs-bench/tree/feature/rtc#usage):

| Update     |    SFU        |  Clients |     Type      |    CPU    |  Memory   | Threads | VM   |
| ---------- | ------------- | -------- | ------------- | --------- | --------  | ------- | ---- |
| 2021-05-10 | SRS/v4.0.105  | 2000     |   players     |   ~94% x1 |   462MB   | 1      | G7 2CPU |
| 2021-05-10 | SRS/v4.0.105  | 1000     |   players     |   ~90% x1 |   180MB   | 1      | G5 2CPU |
| 2021-03-31 | SRS/v4.0.87   | 800      |   players     |   ~94% x1 |   444MB   | 1      | G5 2CPU |
| 2021-05-10 | Janus/v0.11.1 | 700      |   players     |   ~93% x2 |   430MB   | 24     | G5 2CPU |
| 2021-05-10 | SRS/v4.0.105  | 1700     |   publishers  |   ~92% x1 |   334MB   | 1      | G7 2CPU |
| 2021-05-10 | SRS/v4.0.105  | 950      |   publishers  |   ~92% x1 |   132MB   | 1      | G5 2CPU |
| 2021-03-31 | SRS/v4.0.87   | 550      |   publishers  |   ~86% x1 |   1.3GB   | 1      | G5 2CPU |
| 2021-05-10 | Janus/v0.11.1 | 350      |   publishers  |   ~93% x2 |   405MB   | 23     | G5 2CPU |

> Note: CentOS7, 600Kbps, [ECS/G5-2.5GHZ(SkyLake)/G7-2.7GHZ(IceLake)](https://help.aliyun.com/document_detail/25378.html), 
> [SRS/v4.0.87](https://github.com/ossrs/srs/commit/d6c16a7e236e03eba754c763e865464ec82d4516), 
> [SRS/v4.0.105](https://github.com/ossrs/srs/commit/2ad24b2313e88a85801deaea370204f225555939), 
> [Janus/v0.11.1](https://github.com/meetecho/janus-gateway/releases/tag/v0.11.1).

<a name="latency-benchmark"></a>
**Latency benchmark**

The latency between encoder and player with realtime config([CN][v4_CN_LowLatency], [EN][v4_EN_LowLatency]):

|   Update      |    SRS    | Protocol |    VP6    |  H.264    |  VP6+MP3  | H.264+MP3 |
| ------------- | --------- | --------- | --------- | --------- | --------- | --------  |
|   2014-12-16  |   2.0.72  | RTMP      |   0.1s    |   0.4s    |[0.8s][p15]|[0.6s][p16]|
|   2014-12-12  |   2.0.70  | RTMP      |[0.1s][p13]|[0.4s][p14]|   1.0s    |   0.9s    |
|   2014-12-03  |   1.0.10  | RTMP      |   0.4s    |   0.4s    |   0.9s    |   1.2s    |
|   2021-04-02  |   4.0.87  | WebRTC    |   x       |   80ms    |   x       |   x       |

> 2018-08-05, [c45f72e](https://github.com/ossrs/srs/commit/c45f72ef7bac9c7cf85b9125fc9e3aafd53f396f), Refine HTTP-FLV latency, support realtime mode. 2.0.252

We used FMLE as encoder for benchmark. The latency of server was 0.1s+, 
and the bottleneck was the encoder. For more information, read 
[bug #257][bug #257-c0].

## Architecture

The stream architecture of SRS.

```
+----------+             +----------+
| Upstream |             |  Deliver |
+---|------+             +----|-----+
+---+------------------+------+---------------------+----------------+
|     Input            | SRS(Simple RTMP Server)    |     Output     |
+----------------------+----------------------------+----------------+
|                      |   +-> DASH ----------------+-> DASH player  |
|    Encoder(1)        |   +-> RTMP/HDS  -----------+-> Flash player |
|  (FMLE,OBS,   --RTMP-+->-+-> HLS/HTTP ------------+-> M3U8 player  |
|  FFmpeg,XSPLIT,      |   +-> FLV/MP3/Aac/Ts ------+-> HTTP player  |
|  ......)             |   +-> Fowarder ------------+-> RTMP server  |
|                      |   +-> Transcoder ----------+-> RTMP server  |
|                      |   +-> EXEC(5) -------------+-> External app |
|                      |   +-> DVR -----------------+-> FLV file     |
|                      |   +-> BandwidthTest -------+-> Flash        |
|                      |   +-> WebRTC --------------+-> Flash        |
+----------------------+                            |                |
|    WebRTC Client     |   +--> RTMP                |                |
| (H5,Native...) --RTC-+---+---> WebRTC ------------+-> WebRTC Client|
+----------------------+                            |                |
|  MediaSource(2)      |                            |                |
|  (RTSP,FILE,         |                            |                |
|   HTTP,HLS,   --pull-+->-- Ingester(3) -(rtmp)----+-> SRS          |
|   Device,            |                            |                |
|   ......)            |                            |                |
+----------------------+                            |                |
|  MediaSource(2)      |                            |                |
|  (MPEGTSoverUDP      |                            |                |
|   HTTP-FLV,   --push-+->- StreamCaster(4) -(rtmp)-+-> SRS          |
|   GB28181,SRT,       |                            |                |
|   ......)            |                            |                |
+----------------------+                            |                |
|  FFMPEG --push(srt)--+->- SRTModule(5)  ---(rtmp)-+-> SRS          |
+----------------------+----------------------------+----------------+
```

Remark:

1. Encoder: Encoder pushs RTMP stream to SRS.
1. MediaSource: Supports any media source, ingesting by ffmpeg.
1. Ingester: Forks a ffmpeg(or other tools) to ingest as rtmp to SRS, please read [Ingest][v4_CN_Ingest].
1. Streamer: Remuxs other protocols to RTMP, please read [Streamer][v4_CN_Streamer].
1. EXEC: Like NGINX-RTMP, EXEC forks external tools for events, please read [ng-exec][v4_CN_NgExec].
1. SRTModule: A isolate module which run in [hybrid](https://github.com/ossrs/srs/issues/1147#issuecomment-577574883) model.

## AUTHORS

There are two types of people that have contributed to the SRS project:

* Maintainers: Contribute and maintain important features. SRS always remembers and thanks you by writing your names in stream metadata.
* [Contributors][authors]: Submit patches, report bugs, add translations, help answer newbie questions, and generally make SRS much better.

Maintainers of SRS project:

* [Winlin](https://github.com/winlinvip): All areas of streaming server and documents.
* [Wenjie](https://github.com/wenjiegit): The focus of his work is on the [HDS](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_DeliveryHDS) module.
* [Runner365](https://github.com/runner365): The focus of his work is on the [SRT](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_SRTWiki) module.
* [John](https://github.com/xiaozhihong): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.
* [B.P.Y(Bepartofyou)](https://github.com/Bepartofyou): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.
* [Lixin](https://github.com/xialixin): Focus on [GB28181](https://github.com/ossrs/srs/issues/1500) module.
* [Mozhan](https://github.com/lipeng19811218): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.
* [Jinxue](https://github.com/chen-guanghua): Focus on [WebRTC](https://github.com/simple-rtmp-server/srs/wiki/v4_CN_WebRTC) module.

A big THANK YOU goes to:

* All friends of SRS for [big supports][bigthanks].
* Genes amd Mabbott for creating [st][st]([state-threads][st2]).
* [Michael Talyanksy](https://github.com/michaeltalyansky) for introducing ST to us.

## Mirrors

Gitee: [https://gitee.com/ossrs/srs][gitee], the GIT usage([CN][v4_CN_Git], [EN][v4_EN_Git])

```
git clone https://gitee.com/ossrs/srs.git &&
cd srs && git remote set-url origin https://github.com/ossrs/srs.git && git pull
```

> Remark: For users in China, recomment to use mirror from CSDN or OSChina, because they are much faster.

Gitlab: [https://gitlab.com/winlinvip/srs-gitlab][gitlab], the GIT usage([CN][v4_CN_Git], [EN][v4_EN_Git])

```
git clone https://gitlab.com/winlinvip/srs-gitlab.git srs &&
cd srs && git remote set-url origin https://github.com/ossrs/srs.git && git pull
```

Github: [https://github.com/ossrs/srs][srs], the GIT usage([CN][v4_CN_Git], [EN][v4_EN_Git])

```
git clone https://github.com/ossrs/srs.git
```

| Branch | Cost | Size | CMD |
| --- | --- | --- | --- |
| 3.0release | 2m19.931s | 262MB | git clone -b 3.0release https://gitee.com/ossrs/srs.git |
| 3.0release | 0m56.515s | 95MB | git clone -b 3.0release --depth=1 https://gitee.com/ossrs/srs.git |
| develop | 2m22.430s | 234MB | git clone -b develop https://gitee.com/ossrs/srs.git |
| develop | 0m46.421s | 42MB | git clone -b develop --depth=1 https://gitee.com/ossrs/srs.git |
| min | 2m22.865s | 217MB | git clone -b min https://gitee.com/ossrs/srs.git |
| min | 0m36.472s | 11MB | git clone -b min --depth=1 https://gitee.com/ossrs/srs.git |

## System Requirements

Supported operating systems and hardware:

* Linux, with x86, x86-64 or arm.
* Mac, with intel chip.
* Other OS, such as Windows, please use [docker][docker-srs4].

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
[bigthanks]: https://github.com/ossrs/srs/wiki/v4_CN_Product#bigthanks
[st]: https://github.com/winlinvip/state-threads
[st2]: https://github.com/ossrs/state-threads/tree/srs
[state-threads]: https://github.com/ossrs/state-threads/tree/srs
[nginx-rtmp]: https://github.com/arut/nginx-rtmp-module
[http-parser]: https://github.com/joyent/http-parser
[nginx]: http://nginx.org/
[FFMPEG]: http://ffmpeg.org/
[libx264]: http://www.videolan.org/
[srs]: https://github.com/ossrs/srs
[csdn]: https://code.csdn.net/winlinvip/srs-csdn
[gitee]: https://gitee.com/ossrs/srs
[srs-dolphin]: https://github.com/ossrs/srs-dolphin
[oryx]: https://github.com/ossrs/go-oryx
[srs-bench]: https://github.com/ossrs/srs-bench
[srs-ngb]: https://github.com/ossrs/srs-ngb
[srs-librtmp]: https://github.com/ossrs/srs-librtmp
[gitlab]: https://gitlab.com/winlinvip/srs-gitlab
[console]: http://ossrs.net:1985/console
[player]: http://ossrs.net/players/srs_player.html
[modules]: https://github.com/ossrs/srs/blob/develop/trunk/modules/readme.txt
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
[v1-wiki-cn]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[v1-wiki-en]: https://github.com/ossrs/srs/wiki/v4_EN_Home
[v2-wiki-cn]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[v2-wiki-en]: https://github.com/ossrs/srs/wiki/v4_EN_Home
[v4_CN_Home]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[v4_EN_Home]: https://github.com/ossrs/srs/wiki/v4_EN_Home
[v4_CN_Home]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[v4_EN_Home]: https://github.com/ossrs/srs/wiki/v4_EN_Home
[v4_CN_Home]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[v4_EN_Home]: https://github.com/ossrs/srs/wiki/v4_EN_Home
[v4_CN_Home]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[v4_EN_Home]: https://github.com/ossrs/srs/wiki/v4_EN_Home
[donation0]: http://winlinvip.github.io/srs.release/donation/index.html
[donation1]: http://ossrs.net/srs.release/donation/index.html
[donation2]: http://ossrs.net/srs.release/donation/paypal.html
[donations]: https://github.com/ossrs/srs/blob/develop/DONATIONS.txt

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
[v4_CN_LowLatency]: https://github.com/ossrs/srs/wiki/v4_CN_LowLatency
[v4_EN_LowLatency]: https://github.com/ossrs/srs/wiki/v4_EN_LowLatency
[v4_EN_LowLatency#merged-read]: https://github.com/ossrs/srs/wiki/v4_EN_LowLatency#merged-read
[v4_CN_Performance#performancereport4k]: https://github.com/ossrs/srs/wiki/v4_CN_Performance#performancereport4k
[v4_CN_DRM#tokentraverse]: https://github.com/ossrs/srs/wiki/v4_CN_DRM#tokentraverse
[v4_CN_RaspberryPi]: https://github.com/ossrs/srs/wiki/v4_CN_RaspberryPi
[v4_CN_Build]: https://github.com/ossrs/srs/wiki/v4_CN_Build
[v4_CN_LowLatency]: https://github.com/ossrs/srs/wiki/v4_CN_LowLatency
[v4_CN_HowToAskQuestion]: https://github.com/ossrs/srs/wiki/v4_CN_HowToAskQuestion
[v4_CN_Build]: https://github.com/ossrs/srs/wiki/v4_CN_Build
[v4_CN_Performance]: https://github.com/ossrs/srs/wiki/v4_CN_Performance
[v4_CN_RaspberryPi]: https://github.com/ossrs/srs/wiki/v4_CN_RaspberryPi
[v4_CN_LowLatency#merged-read]: https://github.com/ossrs/srs/wiki/v4_CN_LowLatency#merged-read
[v4_CN_Product]: https://github.com/ossrs/srs/wiki/v4_CN_Product
[v4_EN_LowLatency#merged-write]: https://github.com/ossrs/srs/wiki/v4_EN_LowLatency#merged-write
[v4_CN_LowLatency#merged-write]: https://github.com/ossrs/srs/wiki/v4_CN_LowLatency#merged-write
[v4_CN_NgExec]:https://github.com/ossrs/srs/wiki/v4_CN_NgExec
[v4_EN_NgExec]:https://github.com/ossrs/srs/wiki/v4_EN_NgExec
[v4_CN_ReusePort]:https://github.com/ossrs/srs/wiki/v4_CN_ReusePort
[v4_EN_ReusePort]:https://github.com/ossrs/srs/wiki/v4_EN_ReusePort
[v4_CN_SampleSRT]:https://github.com/ossrs/srs/wiki/v4_CN_SampleSRT
[v4_EN_SampleSRT]:https://github.com/ossrs/srs/wiki/v4_EN_SampleSRT
[v4_CN_SampleDASH]:https://github.com/ossrs/srs/wiki/v4_CN_SampleDASH
[v4_EN_SampleDASH]:https://github.com/ossrs/srs/wiki/v4_EN_SampleDASH

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
[bug #834]: https://github.com/ossrs/srs/issues/834
[bug #841]: https://github.com/ossrs/srs/issues/841
[bug #846]: https://github.com/ossrs/srs/issues/846
[bug #844]: https://github.com/ossrs/srs/issues/844
[bug #848]: https://github.com/ossrs/srs/issues/848
[bug #851]: https://github.com/ossrs/srs/issues/851
[bug #636]: https://github.com/ossrs/srs/issues/636
[bug #865]: https://github.com/ossrs/srs/issues/865
[bug #893]: https://github.com/ossrs/srs/issues/893
[bug #899]: https://github.com/ossrs/srs/issues/899
[bug #1033]: https://github.com/ossrs/srs/issues/1033
[bug #1039]: https://github.com/ossrs/srs/issues/1039
[bug #1044]: https://github.com/ossrs/srs/issues/1044
[bug #1045]: https://github.com/ossrs/srs/issues/1045
[bug #1059]: https://github.com/ossrs/srs/issues/1059
[bug #1077]: https://github.com/ossrs/srs/issues/1077
[bug #1176]: https://github.com/ossrs/srs/issues/1176
[bug #1119]: https://github.com/ossrs/srs/issues/1119
[bug #1031]: https://github.com/ossrs/srs/issues/1031
[bug #1110]: https://github.com/ossrs/srs/issues/1110
[bug #910]: https://github.com/ossrs/srs/issues/910
[bug #1202]: https://github.com/ossrs/srs/issues/1202
[bug #1237]: https://github.com/ossrs/srs/issues/1237
[bug #1236]: https://github.com/ossrs/srs/issues/1236
[bug #1250]: https://github.com/ossrs/srs/issues/1250
[bug #1263]: https://github.com/ossrs/srs/issues/1263
[bug #1261]: https://github.com/ossrs/srs/issues/1261
[bug #1274]: https://github.com/ossrs/srs/pull/1274
[bug #1339]: https://github.com/ossrs/srs/pull/1339
[bug #1312]: https://github.com/ossrs/srs/pull/1312
[bug #1304]: https://github.com/ossrs/srs/pull/1304
[bug #1524]: https://github.com/ossrs/srs/issues/1524
[bug #1488]: https://github.com/ossrs/srs/issues/1488
[bug #1551]: https://github.com/ossrs/srs/pull/1551
[bug #1554]: https://github.com/ossrs/srs/pull/1554
[bug #1672]: https://github.com/ossrs/srs/issues/1672
[bug #xxxxxxxxxx]: https://github.com/ossrs/srs/issues/xxxxxxxxxx

[bug #735]: https://github.com/ossrs/srs/issues/735
[bug #742]: https://github.com/ossrs/srs/issues/742
[bug #738]: https://github.com/ossrs/srs/issues/738
[bug #786]: https://github.com/ossrs/srs/issues/786
[bug #820]: https://github.com/ossrs/srs/issues/820
[bug #547]: https://github.com/ossrs/srs/issues/547
[bug #904]: https://github.com/ossrs/srs/issues/904
[bug #821]: https://github.com/ossrs/srs/issues/821
[bug #913]: https://github.com/ossrs/srs/issues/913
[bug #460]: https://github.com/ossrs/srs/issues/460
[bug #775]: https://github.com/ossrs/srs/issues/775
[bug #1057]: https://github.com/ossrs/srs/issues/1057
[bug #105]: https://github.com/ossrs/srs/issues/105
[bug #727]: https://github.com/ossrs/srs/issues/727
[bug #1087]: https://github.com/ossrs/srs/issues/1087
[bug #1051]: https://github.com/ossrs/srs/issues/1051
[bug #1093]: https://github.com/ossrs/srs/issues/1093
[bug #1501]: https://github.com/ossrs/srs/issues/1501
[bug #1229]: https://github.com/ossrs/srs/issues/1229
[bug #1042]: https://github.com/ossrs/srs/issues/1042
[bug #1445]: https://github.com/ossrs/srs/issues/1445
[bug #1506]: https://github.com/ossrs/srs/issues/1506
[bug #1520]: https://github.com/ossrs/srs/issues/1520
[bug #1223]: https://github.com/ossrs/srs/issues/1223
[bug #1508]: https://github.com/ossrs/srs/issues/1508
[bug #1535]: https://github.com/ossrs/srs/issues/1535
[bug #1537]: https://github.com/ossrs/srs/issues/1537
[bug #1538]: https://github.com/ossrs/srs/issues/1538
[bug #1282]: https://github.com/ossrs/srs/issues/1282
[bug #1105]: https://github.com/ossrs/srs/issues/1105
[bug #1544]: https://github.com/ossrs/srs/issues/1544
[bug #1255]: https://github.com/ossrs/srs/issues/1255
[bug #1543]: https://github.com/ossrs/srs/issues/1543
[bug #1509]: https://github.com/ossrs/srs/issues/1509
[bug #1575]: https://github.com/ossrs/srs/issues/1575
[bug #1070]: https://github.com/ossrs/srs/issues/1070
[bug #1580]: https://github.com/ossrs/srs/issues/1580
[bug #1547]: https://github.com/ossrs/srs/issues/1547
[bug #1221]: https://github.com/ossrs/srs/issues/1221
[bug #1111]: https://github.com/ossrs/srs/issues/1111
[bug #463]: https://github.com/ossrs/srs/issues/463
[bug #1147]: https://github.com/ossrs/srs/issues/1147
[bug #1108]: https://github.com/ossrs/srs/issues/1108
[bug #703]: https://github.com/ossrs/srs/issues/703
[bug #878]: https://github.com/ossrs/srs/issues/878
[bug #607]: https://github.com/ossrs/srs/issues/607
[bug #1303]: https://github.com/ossrs/srs/issues/1303
[bug #1230]: https://github.com/ossrs/srs/issues/1230
[bug #1206]: https://github.com/ossrs/srs/issues/1206
[bug #939]: https://github.com/ossrs/srs/issues/939
[bug #1186]: https://github.com/ossrs/srs/issues/1186
[bug #1592]: https://github.com/ossrs/srs/issues/1592
[bug #665]: https://github.com/ossrs/srs/issues/665
[bug #1595]: https://github.com/ossrs/srs/issues/1595
[bug #1601]: https://github.com/ossrs/srs/issues/1601
[bug #1579]: https://github.com/ossrs/srs/issues/1579
[bug #1598]: https://github.com/ossrs/srs/issues/1598
[bug #1615]: https://github.com/ossrs/srs/issues/1615
[bug #1621]: https://github.com/ossrs/srs/issues/1621
[bug #1634]: https://github.com/ossrs/srs/issues/1634
[bug #1594]: https://github.com/ossrs/srs/issues/1594
[bug #1630]: https://github.com/ossrs/srs/issues/1630
[bug #1635]: https://github.com/ossrs/srs/issues/1635
[bug #1651]: https://github.com/ossrs/srs/issues/1651
[bug #1619]: https://github.com/ossrs/srs/issues/1619
[bug #1629]: https://github.com/ossrs/srs/issues/1629
[bug #1780]: https://github.com/ossrs/srs/issues/1780
[bug #1987]: https://github.com/ossrs/srs/issues/1987
[bug #1548]: https://github.com/ossrs/srs/issues/1548
[bug #1694]: https://github.com/ossrs/srs/issues/1694
[bug #2311]: https://github.com/ossrs/srs/issues/2311
[bug #413]: https://github.com/ossrs/srs/issues/413
[bug #2091]: https://github.com/ossrs/srs/issues/2091
[bug #1342]: https://github.com/ossrs/srs/issues/1342
[bug #2093]: https://github.com/ossrs/srs/issues/2093
[bug #2188]: https://github.com/ossrs/srs/issues/2188
[bug #1193]: https://github.com/ossrs/srs/issues/1193
[bug #2304]: https://github.com/ossrs/srs/issues/2304#issuecomment-826009290
[bug #2355]: https://github.com/ossrs/srs/issues/2355
[bug #307]: https://github.com/ossrs/srs/issues/307
[bug #2362]: https://github.com/ossrs/srs/issues/2362
[bug #2370]: https://github.com/ossrs/srs/issues/2370
[bug #yyyyyyyyyyyyy]: https://github.com/ossrs/srs/issues/yyyyyyyyyyyyy

[bug #1631]: https://github.com/ossrs/srs/issues/1631
[bug #1612]: https://github.com/ossrs/srs/issues/1612
[bug #1636]: https://github.com/ossrs/srs/issues/1636
[bug #1657]: https://github.com/ossrs/srs/issues/1657
[bug #1830]: https://github.com/ossrs/srs/issues/1830
[bug #1657-1]: https://github.com/ossrs/srs/issues/1657#issuecomment-720889906
[bug #1657-2]: https://github.com/ossrs/srs/issues/1657#issuecomment-722904004
[bug #1657-3]: https://github.com/ossrs/srs/issues/1657#issuecomment-722971676
[bug #1998]: https://github.com/ossrs/srs/issues/1998
[bug #2106]: https://github.com/ossrs/srs/issues/2106
[bug #2011]: https://github.com/ossrs/srs/issues/2011
[bug #2324]: https://github.com/ossrs/srs/issues/2324
[bug #1500]: https://github.com/ossrs/srs/issues/1500
[bug #zzzzzzzzzzzzz]: https://github.com/ossrs/srs/issues/zzzzzzzzzzzzz

[exo #828]: https://github.com/google/ExoPlayer/pull/828

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


[contact]: https://github.com/ossrs/srs/wiki/v4_CN_Contact
[v4_CN_Contact]: https://github.com/ossrs/srs/wiki/v4_CN_Contact
[v4_EN_Contact]: https://github.com/ossrs/srs/wiki/v4_EN_Contact
[more0]: http://winlinvip.github.io/srs.release/releases/
[more1]: http://ossrs.net/srs.release/releases/

[LICENSE]: https://github.com/ossrs/srs/blob/develop/LICENSE
[LicenseMixing]: https://github.com/ossrs/srs/wiki/LicenseMixing

[srs_CN]: https://github.com/ossrs/srs/wiki/v4_CN_Home
[srs_EN]: https://github.com/ossrs/srs/wiki/v4_EN_Home
[branch1]: https://github.com/ossrs/srs/tree/1.0release
[branch2]: https://github.com/ossrs/srs/tree/2.0release
[release2]: https://github.com/ossrs/srs/wiki/v4_CN_Product#release20
[release3]: https://github.com/ossrs/srs/wiki/v4_CN_Product#release30
[release4]: https://github.com/ossrs/srs/wiki/v4_CN_Product#release40
