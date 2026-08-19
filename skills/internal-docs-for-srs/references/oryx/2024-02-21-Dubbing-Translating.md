---
slug: dubbing-translating
title: Oryx - Revolutionize Video Content with Oryx - Effortless Dubbing and Translating to Multiple Languages Using OpenAI
authors: []
tags: [dubbing, translating, ai, gpt, voice, srs, oryx, multilingual]
custom_edit_url: null
---

# Revolutionize Video Content with Oryx: Effortless Dubbing and Translating to Multiple Languages Using OpenAI

## Introduction

In today's globalized world, content creation is no longer limited by geographical boundaries. As a result, 
engaging a diverse, international audience has become increasingly important for video creators. Whether 
you're a YouTuber, filmmaker, or e-learning content provider, being able to make your content accessible in 
multiple languages can greatly enhance its impact. That's where Oryx comes in – with its advanced 
multilingual dubbing and translation services powered by OpenAI, breaking language barriers is now simpler 
and more cost-effective than ever.

<!--truncate-->

In this blog, we will discuss how Oryx supports dubbing and translating video files from one language to 
another, such as converting a video with English speech to Chinese subtitles and speech. We will explore how 
this technology can help you appeal to a broader audience and unlock the full potential of your content, all 
while being incredibly easy to use and wallet-friendly. So, buckle up as we dive into the world of multilingual 
video content with Oryx!

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

## Step 2: Upload Video Source File

First, please upload the video source file, click the `Dubbing > Video Source > Upload local file > Upload File` to upload.

![](/img/blog-2024-02-21-01.png)

> Note: You can also use other tools to upload the file under the `/data` directory, and use the `Use server file` to directly use it.

## Step 3: Setup OpenAI Secret Key for Dubbing

To use AI services, you must obtain a secret key from OpenAI. Please open the [API keys](https://platform.openai.com/api-keys)
page in your browser and click the `Create new secret key` button. Once the key is created, copy it and set it in Oryx.

Next, configure the OpenAI secret key in `Dubbing Settings > ASR Settings`. Afterward, click on the
`Test OpenAI Service` button, and if no errors are detected during testing, click on `Update` button.

![](/img/blog-2024-02-21-02.png)

## Step 4: Setup the Language to Translate

Setup the video source language from `Dubbing Settings > ASR Settings > Language`, and set the target language 
to translate in `Dubbing Settings > Multilingual Translation > Instructions` by setting the prompts, click on 
`Update` button.

![](/img/blog-2024-02-21-03.png)

## Step 5: Start Dubbing and Download Translated Video

Click the `Start Dubbing` button, as bellow described.

![](/img/blog-2024-02-21-04.png)

If translated audio segment is longer than the original audio, you can click the `Rephrase` or `Merge next` to 
make it shorter, as bellow described.

![](/img/blog-2024-02-21-05.png)

After all segments are ready, no red color background segments, you will be able to download the translated video, 
click the `Download Dubbing Video` button.

![](/img/blog-2024-02-21-06.png)

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one,
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms,
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

In conclusion, Oryx's support for dubbing and translating video files to multiple languages, including the 
conversion of English speech to Chinese subtitles and speech, is a game-changing solution for content creators 
looking to engage a wider audience. By leveraging the advanced capabilities of OpenAI services, Oryx ensures
an efficient, cost-effective, and user-friendly approach to video localization. This remarkable technology not only
breaks language barriers but also fosters cultural exchange and global communication, propelling your content into 
new horizons and opening doors to endless possibilities.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2024-02-21-dubbing-translating)
