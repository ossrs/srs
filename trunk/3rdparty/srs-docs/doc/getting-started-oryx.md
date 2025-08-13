---
title: Oryx
sidebar_label: Oryx
hide_title: false
hide_table_of_contents: false
---

# Oryx

Oryx(SRS Stack) is a video cloud solution that is lightweight, open-source, and based on Go,
Reactjs, SRS, FFmpeg, WebRTC, etc.

## Introduction

Oryx, an open-source out-of-the-box audio and video solution, is built entirely based on various scenarios. 
Common examples include push-pull streaming scenarios that support different protocols and can be embedded into 
websites like WordPress. 

In recording scenarios, it supports merging multiple streams, setting filters, and recording specific streams only. 
For forwarding and virtual live streaming, files and other streams can be sent to different platforms or to Oryx 
itself. With AI automatic subtitles, OpenAI's capabilities can be utilized to automatically recognize and embed 
subtitles into the video stream. One-click automatic HTTPS makes it easy to enable HTTPS capabilities. 

More diverse scenarios will be available in the future.

## FAQ

If you encounter issues while using Oryx, please read the [FAQ](../../../faq-oryx) first.

## Usage

Please select your platform.

> Remark: Please choose the Ubuntu 20 system, as other systems may encounter some strange issues.

### Docker

Strongly recommend running Oryx with docker:

```bash
docker run --restart always -d -it --name oryx -v $HOME/data:/data \
  -p 80:2022 -p 443:2443 -p 1935:1935 -p 8000:8000/udp -p 10080:10080/udp \
  ossrs/oryx:5
```

Then you can open [http://localhost](http://localhost) to use Oryx.

For more details, please refer to [Oryx Docker](https://github.com/ossrs/oryx#usage).

### HELM

Strongly recommend running Oryx with HELM:

```bash
helm repo add srs http://helm.ossrs.io/stable
helm install srs srs/oryx --set persistence.path=$HOME/data \
  --set service.http=80 --set service.https=443 --set service.rtmp=1935 \
  --set service.rtc=8000 --set service.srt=10080
```

Then you can open [http://localhost](http://localhost) to use Oryx.

### Script

For Ubuntu 20+, you can download the [linux-oryx-en.tar.gz](https://github.com/ossrs/oryx/releases/latest/download/linux-oryx-en.tar.gz)
and install it.

### AWS Lightsail

Oryx supports AWS Lightsail, which is a virtual private server (VPS) service offered by AWS. Please 
follow [How to Establish a Video Streaming Service with a Single Click](../../../blog/Oryx-Tutorial).

### DigitalOcean Droplet

Easily set up an Oryx with just one click. For more information, check out
[How to Establish a Video Streaming Service with a Single Click](../../../blog/Oryx-Tutorial).

### aaPanel

Oryx offers a BaoTa plugin, for usage instructions refer to the [Oryx aaPanel Plugin](../../../blog/BT-aaPanel).

## Changelog

For the update log of the Oryx, please refer to [CHANGELOG](https://github.com/ossrs/oryx/blob/main/DEVELOPER.md#changelog).

For specific features supported by a particular version, you can view the CHANGELOG in the version release, see [Releases](https://github.com/ossrs/oryx/releases).

## Features

About the features of Oryx and comparison with SRS，for more details please read [Features](https://github.com/ossrs/oryx?tab=readme-ov-file#features).

### Compare to SRS

Comparing Oryx and SRS, both offer media streaming capabilities at a similar level.
However, Oryx provides a more powerful and feature-rich experience for end users,
eliminating the need to write any code. Users can directly utilize Oryx for your
media services needs.

| Comparison     | Oryx              | SRS           | Notes                                                           |
|----------------|-------------------|---------------|-----------------------------------------------------------------|
| License        | MIT | MIT           | SRS is licenced under MIT, Oryx is MIT.           |
| Live Streaming | Yes               | Yes           | Both support RTMP, HLS, and HTTP-FLV protocols.                 |
| WebRTC         | Yes               | Yes           | WebRTC is supported by both.                                    |
| Auto HTTPS     | Yes               | No            | Oryx supports automatic request and update HTTPS certs.         |
| Console        | Enhanced          | HTTP API      | Oryx offers a more powerful console.                            |
| Authentication | Yes               | HTTP Callback | Oryx has built-in authentication, while SRS uses callbacks.     |
| DVR            | Enhanced          | File-based    | Oryx supports DVR to file and cloud storage.                    |
| Forwarding     | Enhanced          | Basic         | Oryx can forward to multiple platforms via various protocols.   |
| Virtual Live   | Yes               | No            | Oryx provides advanced virtual live streaming capabilities.     |
| WordPress      | Yes               | No            | Oryx offers a WordPress plugin and step-by-step guidelines.     |
| Transcoding    | Yes               | No            | Oryx supports live stream transcoding.                          |
| Transcription  | Yes               | No            | Convert live speech to subtitle and overlay to video stream.    |
| Live Room      | Yes               | No            | Support live room feature.                                      |
| Dubbing        | Yes               | No            | Support dubbing VoD videos.                                     |

### Streaming and Authentication

Oryx support enhanced streaming with authentication, based on SRS callback. Oryx generate and save 
the stream token to Redis, and verify the stream token when user publish stream via RTMP, SRT, or WHIP/WebRTC.

Oryx also proxies and secures all the HTTP API of SRS, so only authenticated user can access the HTTP API 
and the console.

### DVR

Oryx support DVR or Recording, to convert live stream to file, then save to local disk or cloud storage. 
We also support merge multiple republish session to one DVR file, and support set filters for recording specified 
streams.

See [A Step-by-Step Guide to Server-Side Recording and AWS S3 Integration](../../../blog/Record-Live-Streaming) for details.

### Automatic HTTPS

Oryx support automatic HTTPS, just by one click, you can enable HTTPS for your Oryx. Oryx will 
automatically request and update the HTTPS certificate from [Let's Encrypt](https://letsencrypt.org/). Automatic HTTPS
allows WHIP or publish by webpage, and also support WebRTC, and access user's microphones.

See [How to Secure SRS with Let's Encrypt by 1-Click](../../../blog/Oryx-HTTPS) for details.

### Virtual Live Events

You can use prerecorded videos to simulate live events. You can do 7x24 live stream with only 1 video file. You can
also pull stream to your live room, to make the live stream powerful. You can even pull your IP camera stream to your 
live room.

See [Harness the Power of Pre-Recorded Content for Seamless and Engaging Live Streaming Experiences](../../../blog/Virtual-Live-Events) and 
[Easily Stream Your RTSP IP Camera to YouTube, Twitch, or Facebook](../../../blog/Stream-IP-Camera-Events).

### Restream

With Oryx, you can restream to multiple platforms, like YouTube, Twitch, Facebook, etc. Oryx will 
automatically select a stream to forward, so you can publish multiple streams as fault-tolerant or backup 
stream, when a stream is down, Oryx will switch to another one.

See [Effortlessly Restream Live Content Across Multiple Platforms with Oryx](../../../blog/Multi-Platform-Streaming) for details.

### AI Transcription

Oryx supports AI transcription, which is powered by OpenAI, to convert live speech to text and overlay to 
the video stream as a new live stream. With this feature, allows you to engage more audiences, especially for people 
with hearing disabilities or those who are non-native speakers.

See [Creating Accessible, Multilingual Subtitles for Diverse Audiences](../../../blog/live-streams-transcription) for details.

### Transcode

Oryx suppport transcoding live stream, to decrease the bitrate and save bandwidth and cost, or filter the 
live stream content to make it better.

See [Efficient Live Streaming Transcoding for Reducing Bandwidth and Saving Costs](../../../blog/Live-Transcoding) for details.

## AI Products

We are implementing various AI tools and products in the Oryx, and here is the latest status. We will continue 
to update this document.

1. AI Transcript: Implement voice-to-text by connecting to OpenAI's Whisper, and overlay the text captions onto the live broadcast, enabling automatic subtitles for streaming.
   * Status: Completed and available in the Oryx. Refer to [Creating Accessible, Multilingual Subtitles for Diverse Audiences](../../../blog/live-streams-transcription).
1. Streamer AI Asssistant: Easily create a personal, voice-driven GPT AI assistant with Oryx for enhanced language learning, multi-language chats, and convenient assistance in any setting. Perfect for interactive streaming and daily tasks. It offers numerous possibilities for living room and streaming hosts with AI assistance.
   * Status: Beta version available in the Oryx. Refer to [Speak to the Future - Transform Your Browser into a Personal Voice-Driven GPT AI Assistant with Oryx](../../../blog/browser-voice-driven-gpt).
1. VoD Translation: Translate English videos into Chinese for English learning or create multilingual videos, frequently used in education and e-commerce.
   * Beta version available in the Oryx. Refer to [Revolutionize Video Content with Oryx - Effortless Dubbing and Translating to Multiple Languages Using OpenAI](../../../blog/dubbing-translating).
1. Stream OCR: Extract text from images in live streams, enabling real-time text recognition and translation for a variety of applications.
    * Beta version available in the Oryx. Refer to [Oryx - Leveraging OpenAI for OCR and Object Recognition in Video Streams](../../../blog/ocr-video-streams).

If you are interested in our AI products, feel free to join our [Discord](https://discord.gg/yZ4BnPmHAd) server to discuss with us.

## HTTP API

You can open the `System > OpenAPI` to get the Bearer token and try the HTTP API.

You can click the button on the web to request a HTTP API, you can also use the curl or js code to request the 
HTTP API. Please follow the instructions on the web, for example, use curl to request the HTTP API:

```bash
curl http://localhost/terraform/v1/mgmt/versions
```

Or with the Bearer token:

```bash
curl http://localhost/terraform/v1/hooks/srs/secret/query \
  -X POST -H 'Authorization: Bearer xxxxxx' \
  -H 'Content-Type: application/json' --data '{}'
```

> Note: You can open the `System > OpenAPI` to get the Bearer token and try the HTTP API.

> Note: The web may use JWT token, but you can also use Bearer token to request the HTTP API.

In addition to the sample APIs listed on this page, users can perform all web-based actions through the 
HTTP API. To identify the requests and responses for each API, open Google Chrome, navigate to 
`View > Developer > Developer Tools` click on the `Network` tab, and examine the relevant API interactions.

Oryx also proxy the [SRS HTTP API](./http-api.md), which prefix with `/api/v1/` such as:

```bash
curl http://localhost/api/v1/versions
```

Or with the Bearer token:

```bash
curl http://localhost/api/v1/vhosts/ \
  -X GET -H 'Authorization: Bearer xxxxxx' \
  -H 'Content-Type: application/json'
```

> Note: You can open the `System > OpenAPI` to get the Bearer token and try the HTTP API.

Please read the detail about the API from the [SRS HTTP API](./http-api.md).

## HTTP Callback

HTTP Callback refers to the Oryx running within a Docker container, initiating an HTTP request to
a target URL. For instance, the following process illustrates that when OBS publishs an RTMP stream to Oryx,
the Oryx informs your server about the event by sending an HTTP request to the target URL.

```bash
                   +-----------------------+
                   +                       +
+-------+          +     +-----------+     +                 +--------------+
+  OBS  +--RTMP->--+-----+ Oryx +-----+----HTTP--->-----+  Your Server +
+-------+          +     +-----------+     +  (Target URL)   +--------------+
                   +                       +
                   +       Docker          +
                   +-----------------------+
```

All HTTP requests should be:

* `Content-Type: application-json`

All responses should use:

* `Status: 200 OK` and `{"code": 0}` for success.
* Otherwise, error or fail.

See examples in [HTTP Callback](../docs/v7/doc/http-callback#go-example)

### HTTP Callback: Connectivity Check

Occasionally, you might need to verify if the network is accessible and determine the appropriate target URL to
use. By using the curl command inside the Docker container, you can simulate this request and confirm if the
target URL can be accessed by curl or the Oryx.

First, install curl in Oryx:

```bash
docker exec -it oryx apt-get update -y
docker exec -it oryx apt-get install -y curl
```

Then, simulate an HTTP request to your server:

```bash
docker exec -it oryx curl http://your-target-URL
```

You can use any target URL to test, such as:

* Intranet IP: `http://192.168.1.10/check`
* Internet IP: `http://159.133.96.20/check`
* URL via HTTP: `http://your-domain.com/check`
* URL via HTTPS: `https://your-domain.com/check`

Keep in mind that you should test the connection to the target URL within the Oryx Docker, and avoid
running the curl command from a different server.

### HTTP Callback: on_publish

For HTTP callback `on_publish` event:

```json
Request:
{
  "request_id": "3ab26a09-59b0-42f7-98e3-a281c7d0712b",
  "action": "on_publish",
  "opaque": "mytoken",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "livestream",
  "param": "?secret=8f7605d657c74d69b6b48f532c469bc9"
}

Response:
{
  "code": 0
}
```

* Allow publishing if response success.
* Reject publishing if response error.

### HTTP Callback: on_unpublish

For HTTP callback `on_unpublish` event:

```json
Request:
{
  "request_id": "9ea987fa-1563-4c28-8c6c-a0e9edd4f536",
  "action": "on_unpublish",
  "opaque": "mytoken",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "livestream"
}

Response:
{
  "code": 0
}
```

* Ignore any response error.

### HTTP Callback: on_record_begin

For HTTP callback `on_record_begin` event:

```json
Request:
{
  "request_id": "80ad1ddf-1731-450c-83ec-735ea79dd6a3",
  "action": "on_record_begin",
  "opaque": "mytoken",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "livestream",
  "uuid": "824b96f9-8d51-4046-ba1e-a9aec7d57c95"
}

Response:
{
"code": 0
}
```

* Ignore any response error.

### HTTP Callback: on_record_end

For HTTP callback `on_record_end` event:

```json
Request:
{
  "request_id": "d13a0e60-e2fe-42cd-a8d8-f04c7e71b5f5",
  "action": "on_record_end",
  "opaque": "mytoken",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "livestream",
  "uuid": "824b96f9-8d51-4046-ba1e-a9aec7d57c95",
  "artifact_code": 0,
  "artifact_path": "/data/record/824b96f9-8d51-4046-ba1e-a9aec7d57c95/index.mp4",
  "artifact_url": "http://localhost/terraform/v1/hooks/record/hls/824b96f9-8d51-4046-ba1e-a9aec7d57c95/index.mp4"
}

Response:
{
  "code": 0
}
```

* The `uuid` is the UUID of record task.
* The `artifact_code` indicates the error code. If no error, it's 0.
* The `artifact_path` is the path of artifact mp4 in the container.
* The `artifact_url` is the URL path to access the artifact mp4.
* Ignore any response error.

### HTTP Callback: on_ocr

For HTTP callback `on_ocr` event:

```json
Request:
{
  "request_id": "d13a0e60-e2fe-42cd-a8d8-f04c7e71b5f5",
  "action": "on_ocr",
  "opaque": "mytoken",
  "vhost": "__defaultVhost__",
  "app": "live",
  "stream": "livestream",
  "uuid": "824b96f9-8d51-4046-ba1e-a9aec7d57c95",
  "prompt": "What is in the image?",
  "result": "The image shows a scene featuring a character from a film, likely set in a military or high-tech environment."
}

Response:
{
  "code": 0
}
```

* The `uuid` is the UUID of OCR task.
* The `prompt` the AI model used for OCR.
* The `result` is the OCR result.
* Ignore any response error.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/getting-started-oryx)


