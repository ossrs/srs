---
slug: Virtual-Live-Events
title: Oryx - Virtual Live Events - Harness the Power of Pre-Recorded Content for Seamless and Engaging Live Streaming Experiences
authors: []
tags: [live streaming, virual live events, srs]
custom_edit_url: null
---

# Mastering Virtual Live Events: Harness the Power of Pre-Recorded Content for Seamless and Engaging Live Streaming Experiences

## Introduction

Virtual live events refers to converting recorded video files, devices, or network streams into live 
broadcasts and pushing them to live streaming platforms. For example, in e-commerce live streaming, 
you can pre-record product explanations and demonstrations. In educational live streaming, you can 
pre-record lessons and play them in the live classroom. For online speeches and sharing, you can 
play pre-recorded content in the live room. 

Virtual live events allows streamers to have plenty of preparation time, making the content more polished. 
It helps reduce anxiety for inexperienced streamers, prevents network issues, enables 24/7 live streaming, 
reaches a wider audience, and offers more possibilities for live broadcasts.

<!--truncate-->

Oryx enables you to create virtual live events with just one click, broadcasting them to multiple platforms
like YouTube, Twitch, and Facebook. In this blog post, we'll walk you through the steps to create a virtual live
event using Oryx.

In today's fast-paced world, virtual live events are becoming increasingly popular due to the convenience 
they offer. Whether it's a concert, a soccer game, or an online course, you can now create a virtual live 
event with your video files, making them more interactive and accessible to a wider audience. 

## Step 1: Create Oryx by One Click

Creating an Oryx is simple and can be done with just one click if you use Digital Ocean droplet.
Please see [How to Setup a Video Streaming Service by 1-Click](./2022-04-09-Oryx-Tutorial.md) for detail.

You can also use Docker to create an Oryx with a single command line:

```bash
docker run --rm -it -p 80:2022 -p 443:2443 -p 1935:1935 \
  -p 8080:8080 -p 8000:8000/udp -p 10080:10080/udp --name oryx \
  -v $HOME/data:/data ossrs/oryx:5
```

After creating the Oryx, you can access it through `http://your-server-ip/mgmt` via a browser.

## Step 2: Upload your video file

Once you've created your Oryx, open the dashboard and navigate to `Scenarios > VirtualLive > YouTube`. 
Click on `Choose File`, select your video file, and then click `Upload File`.

![](/img/blog-2023-09-11-01.png)

## Step 3: Stream file to YouTube

To stream your file to YouTube, copy the Stream URL and Stream key from your YouTube [Go live](https://studio.youtube.com/channel/UC/livestreaming) page.

![](/img/blog-2023-09-11-02.png)

Open the Oryx dashboard and click on `Scenarios > VirtualLive > YouTube`. And click `Start Virtual Live` in Oryx.

![](/img/blog-2023-09-11-03.png)

Your stream will now be published on YouTube.

![](/img/blog-2023-09-11-04.png)

## Step 4: Check virtual live status

To monitor the status of your virtual live event, simply check the dashboard. You'll be able to see the status of all your virtual live events, ensuring that everything is running smoothly.

![](/img/blog-2023-09-11-05.png)

## Step 5: Stream file to Twitch

After uploading file, you can stream your file to Twitch, copy the Stream key from your 
Twitch [Dashboard](https://www.twitch.tv/dashboard/settings) under `Settings > Stream`.

![](/img/blog-2023-09-11-06.png)

Open the Oryx dashboard and click on `Scenarios > VirtualLive > Twitch`. Click `Start Virtual Live` in Oryx.

![](/img/blog-2023-09-11-07.png)

And your stream will be published on Twitch in the [Stream Manager](https://www.twitch.tv/dashboard/stream).

![](/img/blog-2023-09-11-08.png)

## Step 5: Stream file to Facebook

After uploading file, you can stream your file to Facebook, copy the Stream key from your 
Facebook [Live Producer](https://www.facebook.com/live/producer?ref=OBS) page,
then click `Go live`, and select `Streaming software`.

![](/img/blog-2023-09-11-09.png)

![](/img/blog-2023-09-11-10.png)

Open the Oryx dashboard and click on `Scenarios > VirtualLive > Facebook`. Click `Start Virtual Live` in Oryx, and your stream will be published on Facebook.

![](/img/blog-2023-09-11-11.png)

![](/img/blog-2023-09-11-12.png)

![](/img/blog-2023-09-11-13.png)

## (Optional) Upload Video File by Other Tools

You can also upload the video file to the `/data/upload` folder using tools like FTP or SCP. After that, choose 
`Use server file`, enter the video file's path, and utilize it as a virtual live source.

This method enables you to upload very large files, particularly when webpage-based uploads fail. By using professional 
upload tools, you can resume uploads or accelerate the process with multi-threading and other features.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

In conclusion, virtual live events provide a convenient and efficient way to broadcast pre-recorded content on 
various platforms, making it more polished and accessible to a wider audience. By reducing anxiety for 
inexperienced streamers and enabling 24/7 streaming, virtual live events offer numerous possibilities for 
various industries. Oryx simplifies the process of creating and broadcasting these events, catering 
to the increasing demand for such interactive experiences in today's fast-paced world.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2023-09-11-Virtual-Live-Events)
