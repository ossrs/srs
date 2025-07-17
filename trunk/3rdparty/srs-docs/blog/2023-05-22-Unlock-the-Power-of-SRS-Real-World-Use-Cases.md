---
slug: unlock-the-power-of-srs-real-world-use-cases
title: SRS - Unlock the Power of SRS, Real-World Use Cases.
authors: []
tags: [srs, use-case]
custom_edit_url: null
---

Discover SRS, the all-in-one open-source media server solution for seamless live streaming, content creation, and AI integration, simplifying broadcasting across platforms like YouTube, Facebook, Twitch, and TikTok.

<!--truncate-->

You can also watch the video on YouTube: [Unlock the Power of SRS: Real-World Use Cases and Boosting Your Business with Simple Realtime Server.](https://youtu.be/WChYr6z7EpY)

## Introduction

![](/img/blog-2023-05-22-001.png)

Hello, I'm Winlin!

I founded SRS, which stands for Simple Realtime Server, or "SARS" for short.

SRS is an easy-to-use, efficient, real-time video server that supports various protocols like RTMP, WebRTC, HLS, HTTP-FLV, SRT, and MPEG-DASH.

In this video, I'll talk about some common ways people use SRS.

## Project Compare, an open-source media gateway server.

![](/img/blog-2023-05-22-002.png)

There are several well-known open-source media servers out there, like NginxRTMP for live streaming, Janus and Mediasoup for WebRTC, and of course, SRS for everything.

We started the SRS project back in 2013, initially supporting RTMP and HLS with latencies of 1-3 seconds and 5-10 seconds, respectively. SRS also worked with HTTP-FLV and HTTP-TS, which are similar to RTMP.

In 2020, we expanded our community and added support for WebRTC and SRT, allowing for sub-second latency. SRT latency is around 300-500 ms, while WebRTC latency is between 80-200 ms.

SRS acts as a media gateway, converting between RTMP, SRT, and WebRTC, so you don't need three separate servers.

We've also updated our documentation and website, which you can check out at ossrs.io. SRS has come a long way, but there's still more to do. We're building a global community on Discord, helping many developers and earning their gratitude.

Now, let me introduce you to some of the ways people use SRS.

## Live Origin Cluter, converting streams is essential.

![](/img/blog-2023-05-22-003.png)

The first use case is the origin cluster, which is a group of origin servers.

An origin server is a fundamental and essential server that works as an SRS gateway, receiving and converting streams from publishers.

Unlike Edge or Proxy servers that scale out the origin server, the origin server is critical. By default, an SRS server acts as an origin server, serving as a streaming hub that collects streams from various publishers.

You can send live streams via RTMP, SRT, or WebRTC to the origin server, which then converts them to HLS, HTTP-FLV, RTMP, SRT, and WebRTC.

You can also pull streams from the origin server, transcode, DVR, or forward them to other servers, and even deliver them to a CDN or create a CDN using SRS.

In short, this is a key feature of SRS.

## Vrtual Live Streaming, boosting business with virtual live streaming.

![](/img/blog-2023-05-22-004.png)

Are you a video blogger with lots of high-quality videos? Have you thought about setting up a live room to grow your business?

With Oryx, you can easily turn your video files into live streams and engage your audience without having to live-stream yourself.

Oryx lets you create virtual live-streaming 24/7 in just three steps: upload your videos, set up a live room (like a YouTube live room), and copy-paste the stream key to start live streaming.

It's easy, doesn't require any media streaming expertise, and Oryx can even restream your live streams to other platforms for free since it's open source.

## Video Blogger, empower video bloggers with live streaming.

![](/img/blog-2023-05-22-005.png)

If you own a WordPress site, you may have considered adding live streaming, which was previously not possible due to WordPress's limitations.

But now, with the SRS Player WordPress plugin, you can incorporate live streaming using HTTP-FLV, HLS, and WebRTC, as well as video-on-demand streaming via HTTP-MP4.

Simply embed the live-streaming player with a WordPress shortcode, which you can find in the Oryx console.

This powerful plugin makes live streaming accessible to everyone, even in WooCommerce, a widely-used e-commerce plugin for WordPress, showcasing the impact of open source technology.

## Unity WebRTC Streaming, make Unity work with WebRTC SFU.

![](/img/blog-2023-05-22-006.png)

SRS Unity shows how Unity developers can integrate live streaming with a WebRTC SDK that's compatible with SRS.

You can send the Unity Camera feed to SRS and play the stream in a browser, retrieve the stream from SRS and display it in a Unity game, or enable multiple Unity games to interact using WebRTC.

In this scenario, SRS serves as a WebRTC SFU server, which is essential for WebRTC Unity clients in a production setting.

WebRTC P2P isn't reliable in real-world situations, but an SFU server provides improved network quality, scalability, and support for WebRTC-RTMP conversion.

Plus, you can record WebRTC streams as video-on-demand files, and SRS works with the Unity WebRTC SDK, Unity AR, and VR.

## Remote Broadcasting & Content Creator, enables remote content creation in broadcasting.

![](/img/blog-2023-05-22-007.png)

SRS can be employed in the broadcast industry to develop a remote content creation system.

The original camera feed is sent to SRS and accessed remotely by an editor, who adds watermarks and logos to the edited stream.

With SRT, latency is around 300-500 ms, enabling real-time editing and switching between camera feeds.

Low latency ensures all streams are synchronized, making it a powerful tool for remote content creation.

You can edit from a distance without being on location and produce multiple streams at once, even generating HDR content using HEVC or AV1.

## Video & Audio AI Process, real-time AI processing makes more possibilities.

![](/img/blog-2023-05-22-008.png)

SRS is also useful in the AI field for video and audio processing. Receive streams from SRS and process them with AI models, like using deepfake for face swapping.

SRS is compatible with AI models for audio processing, as shown by the srs-k2 project, which demonstrates how to use SRS with k2-fsa for ASR (Kaldi 2.0, a popular open-source ASR toolkit).

The end-to-end latency of srs-k2 is around 400-800 ms, and it can be used in WebRTC for multi-language real-time communication systems. This allows for conversations with people in various languages and integration with the AIGC system.

## HEVC/AV1 for AV/AR/8K, reduce the cost significantly.

![](/img/blog-2023-05-22-009.png)

To cut your live-streaming expenses by half, think about using HEVC or AV1.

AV1 is a new, open-source, royalty-free video codec, but its hardware decoder isn't as common as HEVC. However, it's quickly gaining traction and looks promising for the future.

HEVC is a widely adopted codec in the industry, supported by OBS through RTMP and SRT. Send the HEVC stream to SRS using OBS, and play the stream with H5 player, mpegjs.js, VLC, or ffplay.

HEVC or AV1 is essential for 8K live-streaming and is becoming popular in the VR/AR field.

## Restream to Multiple Platforms, restream to multiple platforms without extra cost.

![](/img/blog-2023-05-22-010.png)

Do you want to broadcast your live streams on multiple platforms like YouTube, Facebook, Twitch, and TikTok?

The Oryx simplifies this task and doesn't use up your bandwidth since it takes care of the restreaming for you.

## SRS Prometheus Exporter, make SRS an operatable online product.

![](/img/blog-2023-05-22-011.png)

Prometheus, a well-known open-source monitoring system, is natively supported by SRS through its exporter, letting you keep an eye on the SRS server.

Visualize metrics with Grafana, and look forward to Prometheus and Grafana being integrated into the Oryx in the future.

To back the project, think about donating via OpenCollective.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Support, contributions and donations are welcome.

![](/img/blog-2023-05-22-012.png)

Thanks for watching this video on SRS (Simple Realtime Server). If you found it helpful, please give it a thumbs up 
and subscribe to our channel for more short videos like this. See you in the next one!

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2023-05-22-Unlock-the-Power-of-SRS-Real-World-Use-Cases)

