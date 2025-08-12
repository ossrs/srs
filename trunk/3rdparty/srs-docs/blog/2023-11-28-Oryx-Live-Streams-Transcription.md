---
slug: live-streams-transcription
title: Oryx - Revolutionizing Live Streams with AI Transcription - Creating Accessible, Multilingual Subtitles for Diverse Audiences
authors: []
tags: [live, ai, transcription, asr, subtitles]
custom_edit_url: null
---

# Revolutionizing Live Streams with AI Transcription: Creating Accessible, Multilingual Subtitles for Diverse Audiences

## Introduction

In this blog, we're diving into an exciting advancement in live streaming: using Automatic Speech 
Recognition (ASR) to create real-time subtitles. Have you ever wondered how live streams can be more 
inclusive, especially for people with hearing disabilities or those who are non-native speakers? 
The answer lies in an innovative technology that’s reshaping how we experience live content.

<!--truncate-->

We'll focus on a game-changing tool in ASR – OpenAI's Whisper. This isn't just any technology; it's 
a powerful AI service that understands almost every language in the world and transcripts speech 
with remarkable accuracy. Forget the old days of needing expensive professionals for live translation 
and transcription. With OpenAI's Whisper, this process becomes automated, efficient, and cost-effective.

Designed for beginners, our guide will take you through integrating Whisper AI into your live streams 
to recognize and transcribe speech. Then, we'll show you how to use FFmpeg, to overlay these subtitles 
onto your stream. All of this is made simple using the Oryx, which seamlessly connects these 
technologies with just one click.

Join us as we step into the AI-powered future of live streaming, where accessibility and inclusivity 
are key, making your content enjoyable and accessible to a much wider audience.

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

## Step 2: Publish a Live Stream to Oryx

You can use OBS or FFmpeg to publish a live stream to Oryx. You can also set up HTTPS and publish via WebRTC.

![](/img/blog-2023-11-28-01.png)

Once the stream is published, you can preview it using an H5 player or VLC.
Please see [How to Setup a Video Streaming Service by 1-Click](./2022-04-09-Oryx-Tutorial.md) for detail.

## Step 3: Setup OpenAI Secret Key for Whisper ASR

To use Whisper ASR, you must obtain a secret key from OpenAI. Please open the [API keys](https://platform.openai.com/api-keys) 
page in your browser and click the `Create new secret key` button. Once the key is created, copy it and set it in Oryx. 
Then, click the `Test OpenAI Service` button, as shown in the picture below.

![](/img/blog-2023-11-28-03.png)

If the test is successful, you can click the `Start Transcription` button to start the transcription process.

## Step 4: View Live Stream with Subtitles

When HLS segments are generated, Oryx uses FFmpeg to transcode the TS segment into an audio MP4 file. 
It then utilizes OpenAI's Whisper service to convert this into an SRT subtitle. Following this, the subtitle 
is overlaid onto the original TS file, resulting in the creation of a new live stream.

A link is available to play the newly generated live stream with subtitles. You can open this link directly 
in your browser, as shown in the picture below.

![](/img/blog-2023-11-28-05.png)

Open the HLS stream link in the browser to view the live stream with subtitles.

![](/img/blog-2023-11-28-07.png)

You can also use the HTTP API to obtain ASR results for each HLS segment and perform actions like translation 
or integration with your AI systems.

## HLS with WebVTT

You can enable Web Video Text Tracks (WebVTT) for HLS. First, enable WebVTT in `Transcript > Service Settings > WebVTT Subtitle`,
click the checkbox `Enable WebVTT Subtitle`.

Then, open the `Stream Operations` tab, you will get a HLS stream with WebVTT subtitles.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one,
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms,
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

Oryx is now integrated with OpenAI's Whisper and FFmpeg to transcript live streaming, enhancing the 
audience experience by providing AI-driven subtitles. This shift from manual to automated transcription is 
cost-effective and broadens global accessibility, overcoming language and hearing barriers. We're entering 
a future where AI enhances digital inclusivity, enriching how we share and consume content online.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/23-11-28-Oryx-Live-Streams-Transcription)
