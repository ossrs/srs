# Performance

Here are the listed performance benchmark data with corresponding commits for your reference.

* [Performance Guide for x86/x64 Tests.](https://ossrs.net/lts/zh-cn/docs/v4/doc/performance)
* [Performance Guide for RaspberryPi.](https://ossrs.net/lts/zh-cn/docs/v4/doc/raspberrypi)
* If you are interested in multiple processes performance, please refer to [#775](https://github.com/ossrs/srs/issues/775): REUSEPORT or OriginCluster([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/origin-cluster)/[EN](https://ossrs.io/lts/en-us/docs/v4/doc/origin-cluster)).
* For RTC benchmarking, you may use [srs-bench](https://github.com/ossrs/srs-bench/tree/feature/rtc#usage).

## Play RTMP benchmark

The following data reflects the performance of RTMP playback, as tested using the [srs-bench](https://github.com/ossrs/srs-bench) tool:

|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit                                                                               |
| ------------- | --------- | ------------- | ------------- | --------- | --------  |--------------------------------------------------------------------------------------|
|   2014-12-07  |   2.0.67  |   10k(10000)  |   players     |   95%     |   656MB   | [code](https://github.com/ossrs/srs/commit/1311b6fe6576fd7b9c6d299b0f8f2e8d202f4bf8) |
|   2014-12-05  |   2.0.57  |   9.0k(9000)  |   players     |   90%     |   468MB   | [code](https://github.com/ossrs/srs/commit/9ee138746f83adc26f0e236ec017f4d68a300004) |
|   2014-12-05  |   2.0.55  |   8.0k(8000)  |   players     |   89%     |   360MB   | [code](https://github.com/ossrs/srs/commit/58136ec178e3d47db6c90a59875d7e40946936e5) |
|   2014-11-22  |   2.0.30  |   7.5k(7500)  |   players     |   87%     |   320MB   | [code](https://github.com/ossrs/srs/commit/58136ec178e3d47db6c90a59875d7e40946936e5) |
|   2014-11-13  |   2.0.15  |   6.0k(6000)  |   players     |   82%     |   203MB   | [code](https://github.com/ossrs/srs/commit/cc6aca9ad55342a06440ce7f3b38453776b2b2d1) |
|   2014-11-12  |   2.0.14  |   3.5k(3500)  |   players     |   95%     |   78MB    | [code](https://github.com/ossrs/srs/commit/8acd143a7a152885b815999162660fd4e7a3f247) |
|   2014-11-12  |   2.0.14  |   2.7k(2700)  |   players     |   69%     |   59MB    | -                                                                                    |
|   2014-11-11  |   2.0.12  |   2.7k(2700)  |   players     |   85%     |   66MB    | -                                                                                    |
|   2014-11-11  |   1.0.5   |   2.7k(2700)  |   players     |   85%     |   66MB    | -                                                                                    |
|   2014-07-12  |   0.9.156 |   2.7k(2700)  |   players     |   89%     |   61MB    | [code](https://github.com/ossrs/srs/commit/1ae3e6c64cc5cee90e6050c26968ebc3c18281be) |
|   2014-07-12  |   0.9.156 |   1.8k(1800)  |   players     |   68%     |   38MB    | -                                                                                    |
|   2013-11-28  |   0.5.0   |   1.8k(1800)  |   players     |   90%     |   41M     | -                                                                                    |

| Update     |    SFU           |  Clients |     Type      |    CPU    |  Memory   | Threads | VM   |
| ---------- | ---------------- | -------- | ------------- | --------- | --------  | ------- | ---- |
| 2021-05-11 | SRS/v4.0.105     | 4000     |   players     |   ~94% x1 |   419MB   | 1       | G5 8CPU |
| 2021-05-11 | NginxRTMP/v1.2.1 | 2400     |   players     |   ~92% x1 |   173MB   | 1       | G5 8CPU |

> Note: The test was conducted on CentOS7 with a video bitrate of 600Kbps and CPU of G5-2.5GHZ (SkyLake).

## Publish RTMP benchmark

The following data reflects RTMP publishing performance, as tested using the [srs-bench](https://github.com/ossrs/srs-bench) tool:

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

> Note: The test was conducted on CentOS7 with a video bitrate of 600Kbps and CPU of G5-2.5GHZ (SkyLake).

##Play HTTP FLV benchmark

Here is the data for HTTP-FLV playback, as tested using the [srs-bench](https://github.com/ossrs/srs-bench) tool:

|   Update      |    SRS    |    Clients    |     Type      |    CPU    |  Memory   | Commit        |
| ------------- | --------- | ------------- | ------------- | --------- | --------  | ------------  |
|   2014-05-25  |   2.0.171 |   6.0k(6000)  |   players     |   84%     |   297MB   |   [code][p20] |
|   2014-05-24  |   2.0.170 |   3.0k(3000)  |   players     |   89%     |   96MB    |   [code][p19] |
|   2014-05-24  |   2.0.169 |   3.0k(3000)  |   players     |   94%     |   188MB   |   [code][p18] |
|   2014-05-24  |   2.0.168 |   2.3k(2300)  |   players     |   92%     |   276MB   |   [code][p17] |
|   2014-05-24  |   2.0.167 |   1.0k(1000)  |   players     |   82%     |   86MB    |   -           |

## RTC benchmark

The following data reflects WebRTC playback performance, as tested using the [srs-bench](https://github.com/ossrs/srs-bench/tree/feature/rtc#usage) tool:

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

> Note: The test was conducted on CentOS7 with a video bitrate of 600Kbps and CPU of G5-2.5GHZ (SkyLake).

## Latency benchmark

The following benchmark data shows end-to-end latency tested using the srs-bench tool with SRS running in real-time
configuration([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/low-latency)):

|   Update      |    SRS    | Protocol |    VP6    |  H.264    |  VP6+MP3  | H.264+MP3 |
| ------------- | --------- | --------- | --------- | --------- | --------- | --------  |
|   2014-12-16  |   2.0.72  | RTMP      |   0.1s    |   0.4s    |[0.8s][p15]|[0.6s][p16]|
|   2014-12-12  |   2.0.70  | RTMP      |[0.1s][p13]|[0.4s][p14]|   1.0s    |   0.9s    |
|   2014-12-03  |   1.0.10  | RTMP      |   0.4s    |   0.4s    |   0.9s    |   1.2s    |
|   2021-04-02  |   4.0.87  | WebRTC    |   x       |   80ms    |   x       |   x       |

> 2018-08-05, [c45f72e](https://github.com/ossrs/srs/commit/c45f72ef7bac9c7cf85b9125fc9e3aafd53f396f), Refine HTTP-FLV latency, support realtime mode. 2.0.252

> Note: Multiple factors can impact end-to-end latency, such as the encoder, server, protocol, player, and network.

> Note: VLC player is not recommended for low latency use scenarios due to its large buffer, resulting in significant latency.

Winlin 2021

