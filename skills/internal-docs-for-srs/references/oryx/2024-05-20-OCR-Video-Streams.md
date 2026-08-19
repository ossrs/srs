---
slug: ocr-video-streams
title: Oryx - Leveraging OpenAI for OCR and Object Recognition in Video Streams
authors: []
tags: [ocr, ai, gpt, srs, oryx]
custom_edit_url: null
---

# Leveraging OpenAI for OCR and Object Recognition in Video Streams using Oryx

## Introduction

In today's digital world, videos are everywhere. From social media clips to live broadcasts, we consume a 
vast amount of video content daily. But have you ever wondered how we can make sense of all the information 
in these videos? This is where AI comes in. With the help of artificial intelligence, we can now recognize 
text, identify objects, and even describe scenes in video streams.

<!--truncate-->

One powerful tool that makes this process easy is Oryx. In this blog, we'll explore how Oryx can help you 
perform OCR (Optical Character Recognition) on video streams, allowing you to extract valuable information 
in real-time.

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

![](/img/blog-2024-05-20-01.png)

Once the stream is published, you can preview it using an H5 player or VLC.
Please see [How to Setup a Video Streaming Service by 1-Click](./2022-04-09-Oryx-Tutorial.md) for detail.

## Step 3: Setup OpenAI Secret Key for OCR

To use OCR, you must obtain a secret key from OpenAI. Please open the [API keys](https://platform.openai.com/api-keys)
page in your browser and click the `Create new secret key` button. Once the key is created, copy it and set it in Oryx.
Then, click the `Test OpenAI Service` button, as shown in the picture below.

![](/img/blog-2024-05-20-02.png)

If the test is successful, you can click the `Start OCR` button to start the OCR process.

## Step 4: Setup AI Instructions for OCR

Once you've configured your GPT AI assistant, you can update the bellow prompt at the setting webpage
`Service Settings > AI Instructions > Instructions`.

![](/img/blog-2024-05-20-03.png)

To recognize text in video streams, you can use the following instructions:

```text
Recognize the text in the image. Output the identified text directly.
```

Please remember to restart the OCR after updating the AI settings.

## Step 5: View OCR Results by Callback

Once the OCR process is complete, you can view the results by setting up a callback URL in Oryx.

![](/img/blog-2024-05-20-04.png)

You can also view the last OCR result in the dashboard.

![](/img/blog-2024-05-20-05.png)

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one,
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms,
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

In conclusion, using AI to recognize text and objects in video streams is a game-changer. It helps us quickly 
and accurately extract valuable information from videos. Tools like Oryx make this process simple and efficient, 
allowing you to publish live streams and get real-time OCR results with ease. Whether you're looking to identify 
people, read text, or describe scenes, AI-powered OCR can transform how you interact with video content. By 
leveraging these technologies, you can unlock new possibilities and insights from the videos you encounter 
every day.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/24-05-20-OCR-Video-Streams)
