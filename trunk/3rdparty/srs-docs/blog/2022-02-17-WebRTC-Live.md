---
slug: WebRTC-Live
title: SRS - Why and Why NOT use WebRTC for Live Streaming
authors: []
tags: [blog, webrtc, streaming]
custom_edit_url: null
---

# Why and Why NOT use WebRTC for Live Streaming?

Discussion with a friend Thegobot in [discord](https://discord.gg/yZ4BnPmHAd)

<!--truncate-->

## About send stream

For sending stream by H5, only WebRTC works, so it’s ok to use WebRTC if you only want to support H5.

If you also need to support mobile like iOS or Android, FFmpeg is better then WebRTC for live streaming. Also OBS/vmix for multiple scenes.

If you want to support theses live streaming encoders, WebRTC is a absolutely bad solution, because they doesn’t support it, like FFmpeg, OBS, vmix, etc. There is another protocol SRT designed by Haivision, used in many device and project in live streaming.

## About play stream

From the point view of H5, MSE is much better than WebRTC, used by almost all video platforms like YouTube, Twitch, etc.

Maybe there is bug, but it has nothing to do with HTTP-FLV, it’s about the codec not the muxing, it should exists in DASH or HLS also.

If you want to support mobile like iOS or Android, FFmpeg is better solution, for example, ijkplayer. You could use RTMP or HTTP-FLV, it’s stable.

So it depends on your clients, if all your clients is H5, it’s ok to use WebRTC(send)+MSE(play). I don’t think it’s a good idea to use WebRTC as player for live streaming, because the deliver for WebRTC is complex, you need more servers than live streaming.

Finally, about the latency, HTTP streaming is also low latency live streaming, about 1~3s. If want smaller than 1s? I think you’d better think about it, because it might cause the buffer empty and streaming pausing-resuming stuff.

Does 500ms live streaming is essential? And it’s better than 1~3s solution? I really really don’t think so.

## Known Issues

As I know, the issues for WebRTC in live streaming:

1. Slow startup for user to see the first decoded picture, maybe HTTP-FLV/HLS `<100ms`, while WebRTC `>1s`.
1. Not supported by CDN, while some CDN supports HTTP-FLV, but few support WebRTC, the cost is huge(you spend more money).
1. WebRTC needs more servers to deliver, for DTLS/SRTP encryption, QoS algorithm, UDP low performance for linux kernel. To build a WebRTC/UDP CDN, you spend more 10x money to buy servers. Please read [this post](https://github.com/ossrs/srs/blob/develop/trunk/doc/PERFORMANCE.md#performance) for more.
1. Mobile does not works well for WebRTC, especially mobile H5. While for mobile native, why not RTMP or HTTP-FLV, it’s much much simple.
1. Not used for the whole live streaming economy, especially the encoders, they prefer SRT which is also low latency 200~500ms.
1. Low quality for content, because WebRTC perfer low latency, so it drops packets when network is bad. It’s hard to support 8Mbps or higher live streaming.
1. Not friendly for DVR, if you want to DVR your live streaming published by WebRTC, very unhappy experience, please try it.
1. Audio transcoding cost, because WebRTC use Opus, which need to be transcoded to AAC for live streaming.
1. WebRTC stack is not stable, changed over and over, and it also develops more stack like WebTransport/WebCodec, or QUIC/WebAssembly which is smaller and more simple than WebRTC itself.
1. Last one, sometimes network administrator disable all UDP, er, I know there is something like TURN but …. why not use HTTP/HTTPS/WS/WSS which works perfect at everywhere and any devices.

If insist, please go on and give us more feedbacks in future.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

Ultimately, WebRTC is not designed for live streaming, the only scenario to use WebRTC for live streaming is publishing stream by H5, otherwise, consider about RTMP, HTTP-FLV, HLS or DASH.

For live streaming, rather than modern and new tech stack, it’s actually disaster to use WebRTC in mobile H5, and unnecessary for mobile native players.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/22-02-17-WebRTC-Live)


