---
title: RTMP vs HTTP
sidebar_label: RTMP vs HTTP
hide_title: false
hide_table_of_contents: false
---

# RTMP PK HTTP

There are two major methods to deliver video over internet, Live and WebRTC.

* Live streaming: [HLS](./hls.md), [RTMP](./rtmp.md) and [HTTP-FLV](./flv.md) for entertainment.
* WebRTC: [RTC](./webrtc.md), for communication.

Ignore other delivery protocol, which is not used on internet:
* UDP: Private protocols, realtime protocol, latence in ms.
* P2P: FlashP2P of Adobe, others are private protocol. Large latence, in minutes.
* RTSP: Private protocol not for internet.

And the protocol base on HTTP:
* HTTP progressive: Ancient protocol, not used now.
* HTTP stream: Support seek in query string, for instance, http://x/x.mp4?start=x.
* HLS: The HLS is developed by Apple. Both Apple and Android support it.
* HDS: The HLS like developed by Adobe, shit.
* DASH: The HLS like developed by some companies, not used in China.

Compare the delivery methods on internet:

* HLS: Apple HLS, for both live and vod.
* HTTP: HTTP stream, private http stream, for vod.
* RTMP: Adobe RTMP, for live stream.

## RTMP

The RTMP is stream protocol, good for:
* Realtime: RTMP latency can be 0.8-3s.
* DRM: RTMPE/RTMPS encrypt protocol.
* Stable for PC flash.
* Server input: The actual industrial standard for encoder to output to server is RTMP.
* Fault Tolerance: The RTMP edge-origin can support fault tolerance for stream protocol.
* Monitor: The stream protocol can be monitored.

RTMP is bad for:
* Complex: RTMP is more complex than HTTP, especially the edge.
* Hard to cache: Must use edge to cache.

## HTTP

The HTTP stream is the vod stream used for some video website:

HTTP is delivery files, good for:
* High performance: There are lots of good HTTP server, such as nginx, squid, traffic server.
* No small piece of file: The large file is good than pieces of file for HTTP cache.
* Firewall traverse: Almost all firewall never block the HTTP protocol.

HTTP is bad for:
* Large lantency: The http stream atleast N10s latency.
* Player does not support: Only PC flash can play http stream. Mobile platform does not support http stream.

## HLS

HLS is the open standard of Apple. HLS is supported by Android3+.

HLS is good for:
* High performance: Same to HTTP stream.
* Firewall traverse: Same to HTTP stream.
* Mobile Platform standard: Apple IOS/OSX, Android and PC/flash support HLS.

HLS is bad for:
* Large lantency: The http stream atleast N10s latency.
* Pieces of file: CDN does not like small file.

## Use Scenario

See [HTTP](./hls.md)
and [RTMP](./rtmp.md)

I recomment to use these delivery protocols in:
* Encoder always output RTMP for internet server.
* Server always accept RTMP from encoder.
* The cluster use RTMP as internal delivery protocol.
* The low latency application on PC: Use flash to play RTMP.
* Application without low latency required: RTMP or HLS.
* The vod stream on PC: Use HLS or HTTP stream.
* Apple IOS/OSX: Always use HLS. Or use library to play RTMP, like [https://www.vitamio.org](https://www.vitamio.org)
* Android: Always use HLS. Or use library to play RTMP.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/rtmp-pk-http)


