---
slug: how-to-stream-youtube-using-a-web-browser
title: SRS - How to Stream YouTube Using a Web Browser
authors: []
tags: [youtube, rtmp, rtmps, webrtc, srs, ffmpeg]
custom_edit_url: null
---

# How to Live Streaming on YouTube via RTMP or RTMPS Using a Web Browser

> Written by [Winlin](https://github.com/winlinvip) and GPT4

While Open Broadcaster Software (OBS) is a widely used solution for live-streaming to YouTube via RTMP or RTMPS, there is an alternative approach that leverages a web browser. 

This method involves streaming your camera using WebRTC within a webpage, then employing Simple Realtime Server (SRS) to convert WebRTC to RTMP, and using FFmpeg to publish the RTMP stream to YouTube. For those who prefer RTMPS, FFmpeg can be utilized to extract the stream from SRS via RTMP, transcode it to RTMPS, and subsequently publish it to YouTube.

<!--truncate-->

## Step 1: Setting Up SRS

First, obtain SRS by cloning it from the GitHub repository: 

```bash
git clone https://github.com/ossrs/srs.git
```

Then, compile SRS by executing:

```bash
./configure && make
```

Lastly, launch SRS with the command:

```bash
./objs/srs -c conf/rtc2rtmp.conf
```

To confirm the successful installation, access [http://localhost:8080](http://localhost:8080) in your web browser.

## Step 2: Streaming WebRTC to SRS

Open the webpage [http://localhost:8080/players/whip.html](http://localhost:8080/players/whip.html) to transmit your camera stream to SRS via WebRTC. 

![](/img/blog-2023-05-16-001.png)

To preview the RTMP stream, utilize VLC to play `rtmp://localhost/live/livestream`.

## Step 3: Routing RTMP to YouTube

Access the YouTube live-streaming dashboard at [https://youtube.com/livestreaming/dashboard](https://youtube.com/livestreaming/dashboard).

Acquire the stream server (e.g., `rtmp://a.rtmp.youtube.com/live2`) and stream key (e.g., `9xxx-8yyy-3zzz-3iii-7jjj`).

![](/img/blog-2023-05-16-002.png)

Use FFmpeg to extract the RTMP stream from SRS and forward it to YouTube with this command: 

```bash
ffmpeg -i rtmp://localhost/live/livestream -c copy \
  -f flv rtmp://a.rtmp.youtube.com/live2/9xxx-8yyy-3zzz-3iii-7jjj
```

![](/img/blog-2023-05-16-003.png)

## Step 4: Routing RTMPS to YouTube

To transmit the stream via RTMPS, modify the RTMP URL from `rtmp://a.rtmp.youtube.com/live2/9xxx-8yyy-3zzz-3iii-7jjj` to the RTMPS URL, such as `rtmps://a.rtmp.youtube.com:443/live2/9xxx-8yyy-3zzz-3iii-7jjj`.

```bash
ffmpeg -i rtmp://localhost/live/livestream -c copy \
  -f flv rtmps://a.rtmp.youtube.com:443/live2/9xxx-8yyy-3zzz-3iii-7jjj
```

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

By adhering to these instructions, you can effectively live stream to YouTube via RTMP or RTMPS using a web browser. 
This technique offers a practical alternative to OBS, enabling you to harness the power of WebRTC, SRS, and FFmpeg 
for a smooth and efficient streaming experience.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2023-05-16-Stream-YouTube-Using-Web-Browser)
