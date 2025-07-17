---
slug: hls-5s-low-latency
title: Oryx - Unlock Universal Ultra-Low Latency - Achieving 5-Second HLS Live Streams for All - No Special Equipment Needed
authors: []
tags: [hls, llhls, live-streaming, low-latency-streaming, low-latency-hls]
custom_edit_url: null
---

# Unlock Universal Ultra-Low Latency: Achieving 5-Second HLS Live Streams for All, No Special Equipment Needed

## Introduction

In the dynamic world of live streaming, latency is a critical factor that can make or break the viewer experience. 
Traditionally, achieving low latency has been a challenge, especially when striving for broad compatibility across 
devices and platforms. This is where HLS comes into play, a widely adopted format known for its reliability and 
compatibility. However, HLS is often associated with higher latency about 30 seconds, which can be a drawback for 
real-time interactions.

<!--truncate-->

Enter the realm of low-latency HLS streaming, a game-changer for live broadcasts. In this blog, we'll explore how 
you can drastically reduce HLS stream latency to as low as 5 seconds. The best part? This can be achieved using 
common, highly compatible technologies, without the need for specialized equipment. We'll delve into the use of 
the Oryx, a powerful yet straightforward tool that simplifies the process, making ultra-low latency HLS 
streaming accessible with just a click.

If you need even lower latencies and can compromise on compatibility, there are options. For latencies between 
1 to 3 seconds, HTTP-FLV is a great choice. For ultra-low latencies of about 0.5 to 1 second, WebRTC is ideal. 
Each technology is suitable for different requirements and content types.

In this blog, we aim to provide you with a comprehensive guide on choosing the right streaming technology for your 
business, focusing on HLS for achieving the optimal balance between low latency and high compatibility. Whether 
you're streaming a live sports event, an interactive webinar, or a real-time gaming session, understanding these 
technologies will empower you to deliver the best possible live streaming experience to your audience.

## Key Points

To achieve the widest compatibility, we use the most common HLS protocol, not HTTP-FLV, WebRTC, or even the LLHLS 
protocol, but the most standard HLS protocol.

However, we need to change some settings at critical points, such as:

* For OBS encoder, select the x264 encoder and in the advanced settings, set the `GOP (Keyframe interval)` to `1 s`. Choose `Preset` as `fast`, `Profile` as `baseline`, and `Tune` as `zerolatency`. These settings allow OBS to generate shorter segments while maintaining compatibility.
* For Oryx, enable the HLS low latency mode. This will generate HLS segments as short as possible, around 2 seconds each. The minimum delay for HLS is about two segments, allowing for an approximate 5-second delay.
* For HLS player, such as hls.js, set it to start playing from the last segment and configure the maximum buffer time. When the accumulated delay reaches a certain threshold, enable accelerated playback to maintain a stable low latency.

The entire workflow, from streaming to the server to the player, needs optimization to achieve a 5-second low 
latency. Common mistakes in this process include:

* Inability to set the encoder, like OBS, with a large GOP, such as 10 seconds, or having an excessively large encoder buffer, leading to significant overall delay.
* The Oryx is not set to low latency mode. In normal mode, the generated segment durations are longer, or some servers may not produce segments of accurate duration, preferring larger segments instead.
* Using an incorrect player, such as VLC, which has significant delay regardless of the protocol used. Not all players are suitable; we have tested that both hls.js and mpegts.js players have low latency. In the future, we will test and ensure compatibility with more players.

Let's begin achieving a 5-second HLS latency with a few simple setup steps and clicks.

## Step 1: Create Oryx by One Click

Creating an Oryx is simple and can be done with just one click if you use Digital Ocean droplet.
Please see [How to Setup a Video Streaming Service by 1-Click](./2022-04-09-Oryx-Tutorial.md) for detail.

You can also use Docker to create an Oryx with a single command line:

```bash
docker run --restart always -d -it --name oryx -v $HOME/data:/data \
  -p 80:2022 -p 443:2443 -p 1935:1935 -p 8000:8000/udp -p 10080:10080/udp \
  ossrs/oryx:5
```

After creating the Oryx, you can access it through `http://your-server-ip/mgmt` via a browser.

## Step 2: Setup OBS for HLS Low Latency

I suggest using OBS to publish the stream. It's a popular and powerful streaming tool that's easy to set up. 
You should configure OBS as follows:

![](/img/blog-2024-01-06-01.png)

Click the `Settings` button, then go to the `Output` tab and apply the following settings:

* Set the `Output Mode` to `Advanced`. The `Simple` mode does not work for low latency HLS.
* Set the `Video Encoder` to `x264`. Note that the hardware encoder may have a larger delay.
* Set the `Keyframe interval` to `1 s`. Please note that this is the most critical setting for achieving low latency.
* Set the `CPU Usage Preset` to `fast`. This setting is helpful for reducing the delay.
* Set the `Profile` to `baseline`. This setting is helpful for reducing the delay.
* Set the `Tune` to `zerolatency`. This setting is helpful for reducing the delay.

Next, open the `Stream` tab and follow the instructions provided by the Oryx to set the server and 
stream key for publishing the stream.

## Step 3: Setup Oryx in HLS Low Latency Mode

Open the Oryx and navigate to `System > HLS Low Latency`. Enable the HLS low latency mode and 
click the `Submit` button.

![](/img/blog-2024-01-06-02.png)

After that, you can obtain the publish server and stream key for OBS to stream to the Oryx.

![](/img/blog-2024-01-06-03.png)

Please configure OBS and begin publishing the RTMP stream to the Oryx.

## Step 4: Setup the HLS Player

Open the Oryx `Simple Player` in the browser, and you can see the HLS stream with a 5-second delay.

![](/img/blog-2024-01-06-04.png)

The Oryx has configured the HLS player using hls.js for low latency mode. We have applied the following 
settings. If you need to use your own HLS player, please configure it similarly:

* Enable `enableWorker` by setting it to `true`. This improves performance and helps avoid lag or frame drops.
* Activate `lowLatencyMode` by setting it to `true`. This enables Low-Latency HLS part playlist and segment loading.
* Set `liveSyncDurationCount` to `0` to start playback from the last segment.
* Set `maxBufferLength` to `5` to set the maximum buffer length in seconds.
* Set `maxLiveSyncPlaybackRate` to `2` to catch up if the latency is large.

For detailed information, please refer to [this commit](https://github.com/ossrs/oryx/commit/a6b709f516da3c7f36f5c3c599142296148187ee#diff-06095ca53f7d88e4f592f1a432030f541adf2060cb2dfc6c4efd86cd9f074820R40).

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one,
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms,
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

Harnessing the Oryx, OBS, and hls.js, we've made 5-second latency HLS streaming a reality, breaking barriers in live 
broadcast low latency and compatibility. This advancement marks a new era for interactive and engaging 
live events, available to all without specialized equipment, setting a new experience in the streaming world.

## Contact

Join us for further conversation on [Discord](https://discord.gg/bQUPDRqy79). If you'd like to help, please 
feel free to support us through donations on [OpenCollective](https://opencollective.com/srs-server).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2024-01-06-HLS-5s-Low-Latency)
