---
title: Build
sidebar_label: Build
hide_title: false
hide_table_of_contents: false
---

# Build

You can build SRS from source code, but [docker](./getting-started.md) is highly recommend.

## Live Streaming

SRS supports live streaming.

Get SRS source, recommend [Ubuntu20](./install.md):

```
git clone -b develop https://github.com/ossrs/srs.git
```

Build SRS in `srs/trunk`:

```
cd srs/trunk
./configure
make
```

Run SRS server:

```
./objs/srs -c conf/srs.conf
```

Check SRS by [http://localhost:8080/](http://localhost:8080/) or:

```
# Check the process status
./etc/init.d/srs status

# Check the SRS logs
tail -n 30 -f ./objs/srs.log
```

Publish stream by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) :

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

Get SRS source, recommend [Ubuntu20](./install.md):

```
git clone -b develop https://github.com/ossrs/srs.git
```

Build SRS in `srs/trunk`:

```
cd srs/trunk
./configure
make
```

Run SRS server:

```
CANDIDATE="192.168.1.10"
./objs/srs -c conf/srs.conf
```

> Note: Please replace the IP with your server IP.

> Note: About CANDIDATE, please read [CANDIDATE](./webrtc.md#config-candidate)

Check SRS by [http://localhost:8080/](http://localhost:8080/) or:

```
# Check the process status
./etc/init.d/srs status

# Check the SRS logs
tail -n 30 -f ./objs/srs.log
```

If SRS runs on localhost, push stream to SRS by [WebRTC: Publish](http://localhost:8080/players/rtc_publisher.html?autostart=true&stream=livestream&port=8080&schema=http)

> Note: If not localhost, browser(WebRTC) requires HTTPS, please see [WebRTC using HTTPS](./getting-started.md#webrtc-using-https) for detail.

Play stream of SRS by [WebRTC: Play](http://localhost:8080/players/rtc_player.html?autostart=true&stream=livestream&schema=http)

> Note: If use different streams, you're able to do video chat application.

## WebRTC for Live Streaming

SRS supports converting live streaming to WebRTC.

Get SRS source, recommend [Ubuntu20](./install.md):

```
git clone -b develop https://github.com/ossrs/srs.git
```

Build SRS in `srs/trunk`:

```
cd srs/trunk
./configure
make
```

Run SRS server:

```
CANDIDATE="192.168.1.10"
./objs/srs -c conf/rtmp2rtc.conf
```

> Note: Please replace the IP with your server IP.

> Note: About CANDIDATE, please read [CANDIDATE](./webrtc.md#config-candidate)

> Note: If convert RTMP to WebRTC, please use [`rtmp2rtc.conf`](https://github.com/ossrs/srs/issues/2728#rtmp2rtc-cn-guide)

Publish stream by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) :

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

> Note: The file `./doc/source.flv` is under the source repository of SRS.

Play stream by:

* WebRTC: [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true)
* H5(HTTP-FLV): [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)
* H5(HLS): [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.m3u8&port=8080&schema=http)

## WebRTC using HTTPS

If not localhost, for example, to view WebRTC on pad or mobile phone, when SRS is running on remote server.

Get SRS source, recommend [Ubuntu20](./install.md):

```
git clone -b develop https://github.com/ossrs/srs.git
```

Build SRS in `srs/trunk`:

```
cd srs/trunk
./configure
make
```

Run SRS server:

```
CANDIDATE="192.168.1.10"
./objs/srs -c conf/https.rtc.conf
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

## Cross Build

Normally you're able to build SRS on both ARM or MIPS servers.

If need to cross-build SRS for embed devices, pelase read [ARM and CrossBuild](./arm.md).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/getting-started-build)


