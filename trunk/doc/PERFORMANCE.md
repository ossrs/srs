# Performance

The performance benchmark data and corelative commits are listed here.

> Note: Please read **Wiki: Gettting Started( [EN](https://github.com/ossrs/srs/wiki/v4_EN_Home#getting-started) / [CN](https://github.com/ossrs/srs/wiki/v4_CN_Home#getting-started) )** first.

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

