# Performance

The performance benchmark data and corelative commits are listed here.

* See also: [Performance for x86/x64 Test Guide][v4_CN_Performance].
* See also: [Performance for RaspberryPi][v4_CN_RaspberryPi].
* For multiple processes performance, read [#775: REUSEPORT][bug #775] or OriginCluster([CN][v4_EN_OriginCluster]/[EN][v4_EN_OriginCluster]).
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

Winlin 2021


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

[authors]: https://github.com/ossrs/srs/blob/develop/trunk/AUTHORS.txt
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
[v4_CN_LowLatency]: https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency
[v4_EN_LowLatency]: https://ossrs.io/lts/en-us/docs/v4/doc/low-latency
[v4_EN_LowLatency#merged-read]: https://ossrs.io/lts/en-us/docs/v4/doc/low-latency#merged-read
[v4_CN_Performance#performancereport4k]: https://ossrs.net/lts/zh-cn/docs/v4/doc/performance#performancereport4k
[v4_CN_DRM#tokentraverse]: https://ossrs.net/lts/zh-cn/docs/v4/doc/drm#tokentraverse
[v4_CN_RaspberryPi]: https://ossrs.net/lts/zh-cn/docs/v4/doc/raspberrypi
[v4_CN_Build]: https://ossrs.net/lts/zh-cn/docs/v4/doc/install
[v4_CN_LowLatency]: https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency
[v4_CN_Build]: https://ossrs.net/lts/zh-cn/docs/v4/doc/install
[v4_CN_Performance]: https://ossrs.net/lts/zh-cn/docs/v4/doc/performance
[v4_CN_RaspberryPi]: https://ossrs.net/lts/zh-cn/docs/v4/doc/raspberrypi
[v4_CN_LowLatency#merged-read]: https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency#merged-read
[v4_EN_LowLatency#merged-write]: https://ossrs.io/lts/en-us/docs/v4/doc/low-latency#merged-write
[v4_CN_LowLatency#merged-write]: https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency#merged-write
[v4_CN_NgExec]:https://ossrs.net/lts/zh-cn/docs/v4/doc/nginx-exec
[v4_EN_NgExec]:https://ossrs.io/lts/en-us/docs/v4/doc/nginx-exec
[v4_CN_SampleSRT]:https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-srt
[v4_EN_SampleSRT]:https://ossrs.io/lts/en-us/docs/v4/doc/sample-srt
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

