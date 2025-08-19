---
title: Docker
sidebar_label: Docker
hide_title: false
hide_table_of_contents: false
---

# Docker

Please run SRS with docker.

## Live Streaming

SRS supports live streaming.

Run SRS using docker:

```bash
docker run --rm -it -p 1935:1935 -p 1985:1985 -p 8080:8080 ossrs/srs:5
```

> Note: The available images is [here](https://hub.docker.com/r/ossrs/srs/tags).

Use docker of FFmpeg to publish:

```bash
docker run --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -c copy -f flv rtmp://host.docker.internal/live/livestream
```

Or publish stream by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) :

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

> Note: The file `./doc/source.flv` is under the source repository of SRS.

Play stream by:

* RTMP (by [VLC](https://www.videolan.org/)): `rtmp://localhost/live/livestream`
* H5(HTTP-FLV): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)
* H5(HLS): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8&port=8080&schema=http)

## WebRTC

SRS supports WebRTC for video chat.

Run SRS using docker:

```bash
CANDIDATE="192.168.1.10"
docker run --rm -it -p 1935:1935 -p 1985:1985 -p 8080:8080 -p 1990:1990 -p 8088:8088 \
    --env CANDIDATE=$CANDIDATE -p 8000:8000/udp \
    ossrs/srs:5
```

> Note: Please replace the IP with your server IP.

> Note: About CANDIDATE, please read [CANDIDATE](./webrtc.md#config-candidate)

If SRS runs on localhost, push stream to SRS by [WebRTC: Publish](http://localhost:8080/players/rtc_publisher.html?autostart=true&stream=livestream&port=8080&schema=http)

> Note: If not localhost, browser(WebRTC) requires HTTPS, please see [WebRTC using HTTPS](./getting-started.md#webrtc-using-https) for detail.

Play stream of SRS by [WebRTC: Play](http://localhost:8080/players/rtc_player.html?autostart=true&stream=livestream&schema=http)

> Note: If use different streams, you're able to do video chat application.

## WebRTC for Live Streaming

SRS supports coverting live streaming to WebRTC.

Run SRS using docker:

```bash
CANDIDATE="192.168.1.10"
docker run --rm -it -p 1935:1935 -p 1985:1985 -p 8080:8080 \
    --env CANDIDATE=$CANDIDATE -p 8000:8000/udp \
    ossrs/srs:5 ./objs/srs -c conf/rtmp2rtc.conf
```

> Note: Please replace the IP with your server IP.

> Note: About CANDIDATE, please read [CANDIDATE](./webrtc.md#config-candidate)

> Note: If convert RTMP to WebRTC, please use [`rtmp2rtc.conf`](https://github.com/ossrs/srs/issues/2728#rtmp2rtc-en-guide)

Use docker of FFmpeg to publish:

```bash
docker run --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -c copy -f flv rtmp://host.docker.internal/live/livestream
```

Or publish stream by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) :

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

> Note: The file `./doc/source.flv` is under the source repository of SRS.

Play stream by:

* WebRTC: [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true)
* H5(HTTP-FLV): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)
* H5(HLS): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8&port=8080&schema=http)

## WebRTC using HTTPS

When pushing stream to SRS, if not localhost, for example, to view WebRTC on pad or mobile phone, when SRS is running on remote server.

> Note: If only need to play WebRTC stream, HTTP is ok. If wants to push stream, and not localhost, you need HTTPS.

Run SRS using docker:

```bash
CANDIDATE="192.168.1.10"
docker run --rm -it -p 1935:1935 -p 1985:1985 -p 8080:8080 -p 1990:1990 -p 8088:8088 \
    --env CANDIDATE=$CANDIDATE -p 8000:8000/udp \
    ossrs/srs:5 ./objs/srs -c conf/https.docker.conf
```

> Note: Please replace the IP with your server IP.

> Note: About CANDIDATE, please read [CANDIDATE](./webrtc.md#config-candidate)

> Remark: Please use your HTTPS key and cert file, please read
> **[HTTPS API](./http-api.md#https-api)**
> and **[HTTPS Callback](./http-callback.md#https-callback)**
> and **[HTTPS Live Streaming](./flv.md#https-flv-live-stream)**,
> however HTTPS proxy also works perfect with SRS such as Nginx.

Push stream to SRS by [WebRTC: Publish](https://192.168.3.82:8088/players/rtc_publisher.html?autostart=true&stream=livestream&api=1990&schema=https)

Play stream of SRS by [WebRTC: Play](https://192.168.3.82:8088/players/rtc_player.html?autostart=true&stream=livestream&api=1990&schema=https)

> Note: For self-sign certificate, please type `thisisunsafe` to accept it.

> Note: If use different streams, you're able to do video chat application.

## SRT for Live Streaming

SRS supports publishing by SRT for live streaming, and play by SRT or other protocols.

First, start SRS with Docker:

```bash
docker run --rm -it -p 1935:1935 -p 1985:1985 -p 8080:8080 -p 10080:10080/udp \
    ossrs/srs:5 ./objs/srs -c conf/srt.conf
```

Publish stream by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) :

```bash
ffmpeg -re -i ./doc/source.flv -c copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish'
```

Play stream by [ffplay](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download)

```bash
ffplay 'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=request'
```

## Multiple Streams

You can send multiple streams to SRS by using different URLs. There's no need to change any settings; 
just change the URL for the stream you're publishing and playing. It's very easy and straightforward.

* `rtmp://ip/live/livesteam`
* `rtmp://ip/live/livesteamN`
* `rtmp://ip/liveN/livestreamN`
* `rtmp://ip/whatever/doesnotmatter`
* `srt://ip:10080?streamid=#!::r=anyM/streamN,m=publish`
* `http://ip:1985/rtc/v1/whip/?app=anyM&stream=streamN`
* `http://ip:1985/rtc/v1/whep/?app=anyM&stream=streamN`
* `http://ip:8080/anyM/streamN.flv`
* `http://ip:8080/anyM/streamN.m3u8`
* `https://ip:8080/anyM/streamN.flv`
* `https://ip:8080/anyM/streamN.m3u8`

SRS uses a configuration at the virtual host (vhost) level. All applications(app) and streams within the 
same vhost share this configuration. For more information, please refer to the [RTMP URL](./rtmp-url-vhost.md) 
documentation.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/getting-started)


