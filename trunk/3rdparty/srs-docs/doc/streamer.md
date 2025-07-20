---
title: Stream Converter
sidebar_label: Caster
hide_title: false
hide_table_of_contents: false
---

# Stream Caster

Stream Converters listen at special TCP/UDP ports, accept new connections and receive packets, then convert to and push 
RTMP stream to SRS server like a RTMP client.

In short, it converts other protocols to RTMP, works like this:

```text
Client ---PUSH--> Stream Converter --RTMP--> SRS --RTMP/FLV/HLS/WebRTC--> Clients
```

> Note: Some stream protocol contains more than one single stream or even transport connections.

## Use Scenario

There are some use scenarios for stream caster, for example:

* Push MPEG-TS over UDP, by some encoder device.
* Push FLV by HTTP POST, by some mobile device.

> Note: FFmpeg supports PUSH MPEGTS over UDP and FLV by HTTP POST to SRS.

## Build

Stream Converter is always enabled in SRS, while some protocols might need special configure parameters, please read 
instructions of each protocol.

## Protocols

The protocols supported by Stream Converter:

* MPEG-TS over UDP: MPEG-TS stream over UDP protocol.
* FLV by HTTP POST: FLV stream over HTTP protocol.

## Config

The configuration for stream converter:

```
# Push MPEGTS over UDP to SRS.
stream_caster {
    # Whether stream converter is enabled.
    # Default: off
    enabled on;
    # The type of stream converter, could be:
    #       mpegts_over_udp, push MPEG-TS over UDP and convert to RTMP.
    caster mpegts_over_udp;
    # The output rtmp url.
    # For mpegts_over_udp converter, the typically output url:
    #           rtmp://127.0.0.1/live/livestream
    output rtmp://127.0.0.1/live/livestream;
    # The listen port for stream converter.
    # For mpegts_over_udp converter, listen at udp port. for example, 8935.
    listen 8935;
}

# Push FLV by HTTP POST to SRS.
stream_caster {
    # Whether stream converter is enabled.
    # Default: off
    enabled on;
    # The type of stream converter, could be:
    #       flv, push FLV by HTTP POST and convert to RTMP.
    caster flv;
    # The output rtmp url.
    # For flv converter, the typically output url:
    #           rtmp://127.0.0.1/[app]/[stream]
    # For example, POST to url:
    #           http://127.0.0.1:8936/live/livestream.flv
    # Where the [app] is "live" and [stream] is "livestream", output is:
    #           rtmp://127.0.0.1/live/livestream
    output rtmp://127.0.0.1/[app]/[stream];
    # The listen port for stream converter.
    # For flv converter, listen at tcp port. for example, 8936.
    listen 8936;
}
```

Please follow instructions of specified protocols bellow.

## Push MPEG-TS over UDP

You're able to push MPEGTS over UDP to SRS, then covert to RTMP and other protocols.

First, start SRS with configuration for MPEGTS:

```bash
./objs/srs -c conf/push.mpegts.over.udp.conf
```

> Note: About the detail configuration, please read about the `mpegts_over_udp` section of [config](#config).

Then, start to push stream, for example, by FFmpeg:

```bash
ffmpeg -re -f flv -i doc/source.flv -c copy -f mpegts udp://127.0.0.1:8935
```

Finally, play the stream:

* [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?stream=livestream.flv)
* [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?stream=livestream.m3u8)
* [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true)

Please note that each UDP port is bind to a RTMP stream.

> Note: About the development notes, please see [#250](https://github.com/ossrs/srs/issues/250).

## Push HTTP FLV to SRS

You're also able to push HTTP FLV by HTTP POST, which is very simple for mobile device to send HTTP stream.

First, start SRS with configuration for FLV:

```bash
./objs/srs -c conf/push.flv.conf
```

> Note: About the detail configuration, please read about the `flv` section of [config](#config).

Then, start to push stream, for example, by FFmpeg:

```bash
ffmpeg -re -f flv -i doc/source.flv -c copy \
    -f flv http://127.0.0.1:8936/live/livestream.flv
```

Finally, play the stream:

* [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?stream=livestream.flv)
* [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?stream=livestream.m3u8)
* [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true)

> Note: About the development notes, please see [#2611](https://github.com/ossrs/srs/issues/2611).

## Push RTSP to SRS

It's been eliminated, see [#2304](https://github.com/ossrs/srs/issues/2304#issuecomment-826009290).

2015.1

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/streamer)


