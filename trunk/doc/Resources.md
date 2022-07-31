# Resources

The resources of SRS.

## Ports

The ports used by SRS, kernel services:

* `tcp://1935`, for RTMP live streaming server([CN][v4_CN_DeliveryRTMP],[EN][v4_EN_DeliveryRTMP]).
* `tcp://1985`, HTTP API server, for HTTP-API([CN][v4_CN_HTTPApi], [EN][v4_EN_HTTPApi]), WebRTC([CN][v4_CN_WebRTC], [EN][v4_EN_WebRTC]), etc.
* `tcp://8080`, HTTP live streaming server, HTTP-FLV([CN][v4_CN_SampleHttpFlv], [EN][v4_EN_SampleHttpFlv]), HLS([CN][v4_CN_SampleHLS], [EN][v4_EN_SampleHLS]) as such.
* `udp://8000`, WebRTC Media([CN][v4_CN_WebRTC], [EN][v4_EN_WebRTC]) server.

For optional HTTPS services, which might be provided by other web servers:

* `tcp://8088`, HTTPS live streaming server.
* `tcp://1990`, HTTPS API server.

For optional stream caster services, to push streams to SRS:

* `udp://8935`, Stream Caster: [Push MPEGTS over UDP](https://ossrs.net/lts/zh-cn/docs/v4/doc/streamer#push-mpeg-ts-over-udp) server.
* `tcp://554`, Stream Caster: [Push RTSP](https://ossrs.net/lts/zh-cn/docs/v4/doc/streamer#push-rtsp-to-srs) server.
* `tcp://8936`, Stream Caster: [Push HTTP-FLV](https://ossrs.net/lts/zh-cn/docs/v4/doc/streamer#push-http-flv-to-srs) server.
* `udp://10080`, Stream Caster: [Push SRT Media](https://github.com/ossrs/srs/issues/1147#issuecomment-577469119) server.

For external services to work with SRS:

* `udp://1989`, [WebRTC Signaling](https://github.com/ossrs/signaling#usage) server.

## APIs

The API used by SRS:

* `/api/v1/` The HTTP API path.
* `/rtc/v1/` The HTTP API path for RTC.
* `/sig/v1/` The [demo signaling](https://github.com/ossrs/signaling) API.

Other API used by [ossrs.net](https://ossrs.net):

* `/gif/v1` The statistic API.
* `/service/v1/` The latest available version API.
* `/ws-service/v1/` The latest available version API, by websocket.
* `/im-service/v1/` The latest available version API, by IM.
* `/code-service/v1/` The latest available version API, by Code verification.

The statistic path for [ossrs.net](https://ossrs.net):

* `/srs/xxx` The GitHub pages for [srs](https://github.com/ossrs/srs)
* `/wiki/xxx` The GitHub wiki for [srs](https://ossrs.net/lts/zh-cn/docs/v4/doc/getting-started)
* `/release/xxx` The pages for [ossrs.net](https://ossrs.net)
* `/console/xxx` The pages for [console](http://ossrs.net/console/)
* `/player/xxx` The pages for [players and publishers](http://ossrs.net/players/)
* `/k8s/xxx` The template and repository deploy by K8s, like [srs-k8s-template](https://github.com/ossrs/srs-k8s-template)

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

[authors]: https://github.com/ossrs/srs/blob/4.0release/trunk/AUTHORS.txt
[bigthanks]: https://ossrs.net/lts/zh-cn/product#release40
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

[v4_CN_Git]: https://ossrs.net/lts/zh-cn/docs/v4/doc/git
[v4_EN_Git]: https://ossrs.io/lts/en-us/docs/v4/doc/git
[v4_CN_SampleRTMP]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-rtmp
[v4_EN_SampleRTMP]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-rtmp
[v4_CN_SampleRTMPCluster]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-rtmp-cluster
[v4_EN_SampleRTMPCluster]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-rtmp-cluster
[v4_CN_SampleOriginCluster]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-origin-cluster
[v4_EN_SampleOriginCluster]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-origin-cluster
[v4_CN_SampleHLS]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-hls
[v4_EN_SampleHLS]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-hls
[v4_CN_SampleTranscode2HLS]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-transcode-to-hls
[v4_EN_SampleTranscode2HLS]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-transcode-to-hls
[v4_CN_SampleFFMPEG]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-ffmpeg
[v4_EN_SampleFFMPEG]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-ffmpeg
[v4_CN_SampleForward]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-forward
[v4_EN_SampleForward]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-forward
[v4_CN_SampleRealtime]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-realtime
[v4_EN_SampleRealtime]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-realtime
[v4_CN_WebRTC]: https://ossrs.net/lts/zh-cn/docs/v4/doc/webrtc
[v4_EN_WebRTC]: https://ossrs.io/lts/en-us/docs/v4/doc/webrtc
[v4_CN_WebRTC#config-candidate]: https://ossrs.net/lts/zh-cn/docs/v4/doc/webrtc#config-candidate
[v4_EN_WebRTC#config-candidate]: https://ossrs.io/lts/en-us/docs/v4/doc/webrtc#config-candidate
[v4_CN_SampleARM]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-arm
[v4_EN_SampleARM]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-arm
[v4_CN_SampleIngest]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-ingest
[v4_EN_SampleIngest]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-ingest
[v4_CN_SampleHTTP]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-http
[v4_EN_SampleHTTP]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-http
[v4_CN_SampleDemo]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sampleDemo
[v4_EN_SampleDemo]: https://ossrs.io/lts/en-us/docs/v4/doc/sampleDemo
[v4_CN_OriginCluster]: https://ossrs.net/lts/zh-cn/docs/v4/doc/origin-cluster
[v4_EN_OriginCluster]: https://ossrs.io/lts/en-us/docs/v4/doc/origin-cluster
[v4_CN_REUSEPORT]: https://ossrs.net/lts/zh-cn/docs/v4/doc/reuse-port
[v4_EN_REUSEPORT]: https://ossrs.io/lts/en-us/docs/v4/doc/reuse-port
[v4_CN_Sample]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample
[v4_EN_Sample]: https://ossrs.io/lts/en-us/docs/v4/doc/sample

[v4_CN_Build]: https://ossrs.net/lts/zh-cn/docs/v4/doc/install
[v4_EN_Build]: https://ossrs.io/lts/en-us/docs/v4/doc/install
[v4_CN_Performance]: https://ossrs.net/lts/zh-cn/docs/v4/doc/performance
[v4_EN_Performance]: https://ossrs.io/lts/en-us/docs/v4/doc/performance
[v4_CN_DeliveryRTMP]: https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-rtmp
[v4_EN_DeliveryRTMP]: https://ossrs.io/lts/en-us/docs/v4/doc/delivery-rtmp
[v4_CN_Edge]: https://ossrs.net/lts/zh-cn/docs/v4/doc/edge
[v4_EN_Edge]: https://ossrs.io/lts/en-us/docs/v4/doc/edge
[v4_CN_RtmpUrlVhost]: https://ossrs.net/lts/zh-cn/docs/v4/doc/rtmp-url-vhost
[v4_EN_RtmpUrlVhost]: https://ossrs.io/lts/en-us/docs/v4/doc/rtmp-url-vhost
[v4_CN_RTMPHandshake]: https://ossrs.net/lts/zh-cn/docs/v4/doc/rtmp-handshake
[v4_EN_RTMPHandshake]: https://ossrs.io/lts/en-us/docs/v4/doc/rtmp-handshake
[v4_CN_HTTPServer]: https://ossrs.net/lts/zh-cn/docs/v4/doc/http-server
[v4_EN_HTTPServer]: https://ossrs.io/lts/en-us/docs/v4/doc/http-server
[v4_CN_DeliveryHLS]: https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hls
[v4_EN_DeliveryHLS]: https://ossrs.io/lts/en-us/docs/v4/doc/delivery-hls
[v4_CN_DeliveryHLS2]: https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hls#hlsaudioonly
[v4_EN_DeliveryHLS2]: https://ossrs.io/lts/en-us/docs/v4/doc/delivery-hls#hlsaudioonly
[v4_CN_Reload]: https://ossrs.net/lts/zh-cn/docs/v4/doc/reload
[v4_EN_Reload]: https://ossrs.io/lts/en-us/docs/v4/doc/reload
[v4_CN_LowLatency2]: https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency#gop-cache
[v4_EN_LowLatency2]: https://ossrs.io/lts/en-us/docs/v4/doc/low-latency#gop-cache
[v4_CN_Forward]: https://ossrs.net/lts/zh-cn/docs/v4/doc/forward
[v4_EN_Forward]: https://ossrs.io/lts/en-us/docs/v4/doc/forward
[v4_CN_FFMPEG]: https://ossrs.net/lts/zh-cn/docs/v4/doc/ffmpeg
[v4_EN_FFMPEG]: https://ossrs.io/lts/en-us/docs/v4/doc/ffmpeg
[v4_CN_HTTPCallback]: https://ossrs.net/lts/zh-cn/docs/v4/doc/http-callback
[v4_EN_HTTPCallback]: https://ossrs.io/lts/en-us/docs/v4/doc/http-callback
[v4_CN_SampleDemo]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sampleDemo
[v4_EN_SampleDemo]: https://ossrs.io/lts/en-us/docs/v4/doc/sampleDemo
[v4_CN_SrsLinuxArm]: https://ossrs.net/lts/zh-cn/docs/v4/doc/arm
[v4_EN_SrsLinuxArm]: https://ossrs.io/lts/en-us/docs/v4/doc/arm
[v4_CN_LinuxService]: https://ossrs.net/lts/zh-cn/docs/v4/doc/service
[v4_EN_LinuxService]: https://ossrs.io/lts/en-us/docs/v4/doc/service
[v4_CN_RTMP-ATC]: https://ossrs.net/lts/zh-cn/docs/v4/doc/rtmp-atc
[v4_EN_RTMP-ATC]: https://ossrs.io/lts/en-us/docs/v4/doc/rtmp-atc
[v4_CN_HTTPApi]: https://ossrs.net/lts/zh-cn/docs/v4/doc/http-api
[v4_EN_HTTPApi]: https://ossrs.io/lts/en-us/docs/v4/doc/http-api
[v4_CN_Ingest]: https://ossrs.net/lts/zh-cn/docs/v4/doc/ingest
[v4_EN_Ingest]: https://ossrs.io/lts/en-us/docs/v4/doc/ingest
[v4_CN_DVR]: https://ossrs.net/lts/zh-cn/docs/v4/doc/dvr
[v4_EN_DVR]: https://ossrs.io/lts/en-us/docs/v4/doc/dvr
[v4_CN_SrsLog]: https://ossrs.net/lts/zh-cn/docs/v4/doc/log
[v4_EN_SrsLog]: https://ossrs.io/lts/en-us/docs/v4/doc/log
[v4_CN_DRM2]: https://ossrs.net/lts/zh-cn/docs/v4/doc/drm#tokentraverse
[v4_EN_DRM2]: https://ossrs.io/lts/en-us/docs/v4/doc/drm#tokentraverse
[v4_CN_SampleHTTP]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-http
[v4_EN_SampleHTTP]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-http
[v4_CN_FlvVodStream]: https://ossrs.net/lts/zh-cn/docs/v4/doc/flv-vod-stream
[v4_EN_FlvVodStream]: https://ossrs.io/lts/en-us/docs/v4/doc/flv-vod-stream
[v4_CN_Security]: https://ossrs.net/lts/zh-cn/docs/v4/doc/security
[v4_EN_Security]: https://ossrs.io/lts/en-us/docs/v4/doc/security
[v4_CN_DeliveryHttpStream]: https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-http-flv
[v4_EN_DeliveryHttpStream]: https://ossrs.io/lts/en-us/docs/v4/doc/delivery-http-flv
[v4_CN_DeliveryHDS]: https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hds
[v4_EN_DeliveryHDS]: https://ossrs.io/lts/en-us/docs/v4/doc/delivery-hds
[v4_CN_Streamer]: https://ossrs.net/lts/zh-cn/docs/v4/doc/streamer
[v4_EN_Streamer]: https://ossrs.io/lts/en-us/docs/v4/doc/streamer
[v4_CN_Streamer2]: https://ossrs.net/lts/zh-cn/docs/v4/doc/streamer#push-http-flv-to-srs
[v4_EN_Streamer2]: https://ossrs.io/lts/en-us/docs/v4/doc/streamer#push-http-flv-to-srs
[v4_CN_SampleHttpFlv]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-http-flv
[v4_EN_SampleHttpFlv]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-http-flv
[v4_CN_SampleHttpFlvCluster]: https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-http-flvCluster
[v4_EN_SampleHttpFlvCluster]: https://ossrs.io/lts/en-us/docs/v4/doc/sample-http-flvCluster
[v4_CN_SampleDASH]:https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-dash
[v4_EN_SampleDASH]:https://ossrs.io/lts/en-us/docs/v4/doc/sample-dash

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

[LICENSE]: https://github.com/ossrs/srs/blob/4.0release/LICENSE
[LicenseMixing]: https://ossrs.net/lts/zh-cn/license