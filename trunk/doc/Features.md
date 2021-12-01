# Features

The features of SRS.

> Note: Please read **Wiki: Gettting Started( [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home#getting-started) / [CN](https://github.com/ossrs/srs/wiki/v4_CN_Home#getting-started) )** first.

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
- [x] Support a simple [mgmt console][console], please read [srs-console][srs-ngb].
- [x] [Experimental] Support playing stream by WebRTC, [#307][bug #307].
- [x] [Experimental] Support publishing stream by WebRTC, [#307][bug #307].
- [x] [Experimental] Support mux RTP/RTCP/DTLS/SRTP on one port for WebRTC, [#307][bug #307].
- [x] [Experimental] Support client address changing for WebRTC, [#307][bug #307].
- [x] [Experimental] Support transcode RTMP/AAC to WebRTC/Opus, [#307][bug #307].
- [x] [Experimental] Support AV1 codec for WebRTC, [#2324][bug #2324].
- [x] [Experimental] Enhance HTTP Stream Server for HTTP-FLV, HTTPS, HLS etc. [#1657][bug #1657].
- [x] [Experimental] Support DVR in MP4 format, read [#738][bug #738].
- [x] [Experimental] Support MPEG-DASH, the future live streaming protocol, read [#299][bug #299].
- [x] [Experimental] Support pushing MPEG-TS over UDP, please read [bug #250][bug #250].
- [x] [Experimental] Support pushing FLV over HTTP POST, please read wiki([CN][v4_CN_Streamer2], [EN][v4_EN_Streamer2]).
- [x] [Experimental] Support SRT server, read [#1147][bug #1147].
- [x] [Experimental] Support transmux RTC to RTMP, [#2093][bug #2093].
- [x] [Deprecated] Support Adobe HDS(f4m), please read wiki([CN][v4_CN_DeliveryHDS], [EN][v4_EN_DeliveryHDS]) and [#1535][bug #1535].
- [x] [Deprecated] Support bandwidth testing, please read [#1535][bug #1535].
- [x] [Deprecated] Support Adobe FMS/AMS token traverse([CN][v4_CN_DRM2], [EN][v4_EN_DRM2]) authentication, please read [#1535][bug #1535].
- [x] [Removed] Support pushing RTSP, please read [#2304](https://github.com/ossrs/srs/issues/2304#issuecomment-826009290).
- [x] [Removed] Support HTTP RAW API, please read [#2653](https://github.com/ossrs/srs/issues/2653).
- [x] [Removed] Support RTMP client library: [srs-librtmp][srs-librtmp].
- [ ] Support Windows/Cygwin 64bits, [#2532](https://github.com/ossrs/srs/issues/2532).
- [ ] Support push stream by GB28181, [#1500][bug #1500].
- [ ] Support IETF-QUIC for WebRTC Cluster, [#2091][bug #2091].
- [ ] Enhanced forwarding with vhost and variables, [#1342][bug #1342].
- [ ] Support DVR to Cloud Storage, [#1193][bug #1193].
- [ ] Support H.265 over RTMP and HLS, [#465][bug #465].
- [ ] Improve RTC performance to 5K by multiple threading, [#2188][bug #2188].
- [ ] Support source cleanup for idle streams, [#413][bug #413].
- [ ] Support change user to run SRS, [#1111][bug #1111].
- [ ] Support HLS variant, [#463][bug #463].

> Remark: About the milestone and product plan, please read ([CN][v4_CN_Product], [EN][v4_EN_Product]) wiki.

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

[release2]: https://github.com/ossrs/srs/wiki/v4_CN_Product#release20
[release3]: https://github.com/ossrs/srs/wiki/v4_CN_Product#release30
[release4]: https://github.com/ossrs/srs/wiki/v4_CN_Product#release40