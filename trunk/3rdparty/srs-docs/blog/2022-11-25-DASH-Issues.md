---
slug: why-dash-is-bad-solution-for-live-streaming
title: SRS - Why DASH Is Bad Solution for Live Streaming
authors: []
tags: [video, dash, live, streaming]
custom_edit_url: null
---

# Why DASH Is Bad Solution for Live Streaming

> Written by [John](https://github.com/xiaozhihong), [Winlin](https://github.com/winlinvip)

What is MPEG-DASH(Dynamic Adaptive Streaming over HTTP)? Because Apple HLS is not good enough, so some guys wanted to 
fix it and created an even worse protocol, named DASH or MPEG-DASH.

Well, it's just a joke for any new technology, especially when it's new and there are some issues. However, it's really
true for DASH today, at 2022.11, because we're really suffering while maintaining it.

But, DASH is becoming more and more popular protocol for live streaming, and we believe that all issues will be fixed 
in not very far future, so let's take a look about these issues.

<!--truncate-->

## Good News: Normal Scenario

DASH is available for both VoD and LIVE streaming, and it works for `NORMAL` use scenarios. Let's have a glance.

```bash
docker run --rm -it -p 8080:8080 -p 1935:1935 \
  -e SRS_HTTP_SERVER_ENABLED=on -e SRS_VHOST_DASH_ENABLED=on \
  ossrs/srs:5
```

Publish a live stream by RTMP to media server:

```bash
docker run --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -c copy -f flv rtmp://host.docker.internal/live/livestream
```

Finally you're able to play DASH by:

* VLC: `http://localhost:8080/live/livestream.mpd`
* FFmpeg: `ffplay http://localhost:8080/live/livestream.mpd`
* H5 [srs-player](http://localhost:8080/players/srs_player.html?stream=livestream.mpd&autostart=true)

It's very easy, right?!

## Bad News: Republishing

What's the issues of DASH?

If you open [srs-player](http://localhost:8080/players/srs_player.html?stream=livestream.mpd&autostart=true) and wait 
for about serval minutes while stream is republished, you will find out that the player might be stuck.

Yep, it's maybe the problem of srs-player, which use [dash.js@v4.5.1](https://github.com/Dash-Industry-Forum/dash.js), 
so let's choose another player.

What about VLC? Also fails when stream is republishing.

What about ffplay? Also fails.

All these issues are literally caused by republishing, that is, when you stop the stream, and then publish the stream, 
players might be stuck.

> Note: Player will resume to play when refresh the page.

For HLS, the `EXT-X-DISCONTINUITY` is design for this situation, and all players work very well when stream republishing.

For DASH, the spec introduces a new Period, but it does work whatever. So SRS will regenerate a new MPD file, it makes
the dash.js happy, but doesn't work for VLC or ffplay.

## Bad News: MPD Mode

If use `SegmentTimeline` and use `$Time$` mode, there is bug for `VLC 3.0.17`, but `VLC 4.0` is ok. For more detail 
information about this issue, please see [VLC#2845](https://code.videolan.org/videolan/vlc/-/merge_requests/2845).

If use `SegmentTemplate` without `SegmentTimeline`, player will guess the file name, but it literally fails while 
streaming for a long time. SRS 4.0 uses this mode and there is some bugs about it, see [SRS#1864](https://github.com/ossrs/srs/issues/1864).
So SRS 5.0 fix this issue and use another mode.

SRS 5.0 use `SegmentTemplate` with `SegmentTimeline`, and use `$Number$` but not `$Time$` mode, because there is also 
bug for `$Time$` mode for VLC as previous description.

Right now, at 2022.11, the best compatible mode is `SegmentTemplate` with `SegmentTimeline` and `$Number$` mode, which
is used for SRS 5.0. H5(dash.js) works well, but VLC/ffplay still have some issues.

## About HEVC

[Chrome 105+](https://caniuse.com/?search=HEVC) supports HEVC by default, see [this post](https://zhuanlan.zhihu.com/p/541082191).

You're able to play HEVC over mp4 directly by H5 video, or by MSE if HTTP-FLV/HTTP-TS/HLS etc. Please use [mpegts.js](https://github.com/xqq/mpegts.js) 
to play HEVC over HTTP-FLV or HTTP-TS, see [SRS#465](https://github.com/ossrs/srs/issues/465#usage).

For DASH, the protocol does not limit to use H.264 or HEVC, because it use MP4 which can be play by MSE by Chrome 105+.

However, there still some extra work for media server, to support MP4 or DASH with HEVC. SRS 6.0 is still working on it,
for detail information please see [SRS#465](https://github.com/ossrs/srs/issues/465#status-of-hevc-in-srs).

## Publish by H5 then Delivery by DASH

If wants to publish live stream by H5, then delivery by MPEG-DASH, there is a solution:

```bash
Camera/Microphone --WebRTC---> SRS --DASH--> Viewers
```

Because the only way is to use WebRTC to access the camera and microphone for H5, so you must use WebRTC to ingest live
stream to media server such as SRS. Then SRS will covert WebRTC to RTMP, finally covert to MPEG-DASH, plese see [this post](../docs/v5/doc/getting-started#webrtc-for-live-streaming).

Note that SRS also support [WebRTC-HTTP ingestion protocol (WHIP)](https://datatracker.ietf.org/doc/draft-ietf-wish-whip/),
for example, [srs-unity](https://github.com/ossrs/srs-unity) uses WHIP to publish Unity camera stream.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

DASH is a relative new and good live streaming protocol. After about two years, SRS 5.0 has always been fixing bug and 
now we think it's ready and stable to use DASH in your online product if you want.

## Contact

If you'd like to discuss with SRS, you are welcome to [discord](https://discord.gg/yZ4BnPmHAd)

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2022-11-25-DASH-Issues)

