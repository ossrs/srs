---
title: Learning Path
sidebar_label: Learning Path
hide_title: false
hide_table_of_contents: false
---

# Learning Path

A learning path for newcomers, please be sure to follow the documentation.

## Quick Preview

First, It takes about 5 to 15 minutes to see what live streaming and WebRTC look like, as the following picture shown:。

![](/img/doc-learning-path-001.png)

> Note: This may seem easy, even if you can open two pages directly from the SRS website, but you must build it yourself with SRS, not just open the online demo page.

How do you do it？Please refer to [Getting Started](./getting-started.md)。

The first step in approaching something new is to have an intuitive experience and feel for it. Although it seems simple, it involves almost the whole chain of things in the audio/video field：
- FFmpeg, a powerful audio/video client that supports publish and pull streaming, codecs encoding and decoding , as well as various processing capabilities.
- Chrome (or browser), H5 is the most convenient client, very convenient for demo and learning, SRS’s features basically have H5 demo.
- Audio and video protocols: RTMP, HTTP-FLV, HLS and WebRTC.
- SRS server, deploying audio and video cloud by itself, or providing cloud services for audio and video, SRS is essentially a kind of server for video cloud.

> Note: The above diagram is still missing the mobile end, in fact, the mobile end is just a kind of end, and there is no new protocol, you can also download the SRS live streaming client, experience the above push stream and play, you can also enter your server's stream address to play.

## Deeper

Second, understand each typical scenario of audio and video applications, about five core scenarios, which takes about 3~7 days in total:

Typical audio and video business scenarios, including but not limited to:
- All-platform live streaming. The Encoders (FFmpeg/OBS) above can publish RTMP to SRS; an SRS Origin (no need Cluster), which is muxed into HTTP-FLV streams and HLS; Players can choose HTTP-FLV or HLS streams to play according to the platform's player.
- WebRTC call services, one-to-one calls, multi-person calls, conference rooms, etc. WebRTC is the key and core capability introduced in SRS4. From 1 to 3 seconds latency at the beginning, to 100 to 300 milliseconds latency now, it is definitely not a change of numbers, but an essential change.
- Monitoring and broadcasting business to the cloud. In addition to using FFmpeg to actively pull streams to SRS, you can also use the SRT protocol of the broadcasting industry to publish streams, or the GB28181 protocol of the surveillance industry to publish streams, SRS can converts it to the Internet protocols for playing.
- Low latency live streaming and interactive live streaming. Convert RTMP to WebRTC for playing to reduce the latency of palying, and can also use the WebRTC to publish stream. In the future will support WebTransport live streaming.
- Large-Scale Business, if business grows rapidly, you can use SRS Edge Cluster to support massice Players, or use SRS Origin Cluster to support massive Encoders, of course, you can migrate your business to the video cloud smoothly too. In the future, SRS will also support WebRTC cluster.

Each scenario can build a typical application.

## For Details

Third, Understand the technical points, application scenarios, code and problem solving, about 3 to 6 months.

- [Video Columns](./introduction.md#effective-srs), includes environment building, code analysis, and explanations from professional teachers at Voice Academy.
- [Solution Guides](./introduction.md#solution-guides)，share and explore the application of SRS in different scenarios.
- [Deployment Guides](./introduction.md#deployment-guides), how to deploy to implement different specific functions.
- [Cluster Guides](./introduction.md#cluster-guides), when business grows rapidly, how to scale single server to cluster, and how to serve users in different regions.
- [Integration Guides](./introduction.md#integration-guides), How to integrate with existing systems, how to authenticate users, security and anti-stealing chain mechanisms, etc.
- [Develop Guides](./introduction.md#develop-guide), Concurrent principles, code analysis, high performance server framework, performance optimization, etc.

If you can thoroughly understand SRS, it's really not difficult.

Author：winlinvip

Origin Link：https://www.jianshu.com/p/2662df9fe078

From：jianshu.com

The copyright belongs to the author. For commercial reproduction, please contact the author for authorization, and for non-commercial reproduction, please cite the source.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/learning-path)


