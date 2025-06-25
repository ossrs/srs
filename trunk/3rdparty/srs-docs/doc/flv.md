---
title: HTTP-FLV
sidebar_label: HTTP-FLV
hide_title: false
hide_table_of_contents: false
---

# HTTP-FLV

HTTP-FLV is a live streaming protocol, sometimes simply called FLV, which is used to transmit live streams in FLV 
format over an HTTP connection.

Unlike file downloads, live streams have an indefinite or uncertain length, so they are usually implemented using 
the HTTP Chunked protocol. Similar to HTTP-FLV, there are also HTTP-TS and HTTP-MP3. TS is mainly used in broadcasting 
and television, while MP3 is mainly used in the audio field.

Different from HLS, which is essentially an HTTP file download, HTTP-FLV is a streaming protocol. CDN support for 
HTTP file downloads is well-developed, making HLS more compatible than HTTP-FLV. However, HTTP-FLV has lower latency 
than HLS, typically achieving a delay of around 3 to 5 seconds, while HLS latency is generally 8 to 10 seconds or more.

In terms of protocol implementation, RTMP and HTTP-FLV are very similar. RTMP is based on the TCP protocol, and 
HTTP-FLV is based on HTTP, which is also a TCP protocol. Therefore, their characteristics are very similar. RTMP 
is generally used for streaming and live production because most live production devices support RTMP. For playback 
and consumption, HTTP-FLV or HLS is used because playback devices have better support for HTTP.

HTTP-FLV is highly compatible, supported by almost all platforms and browsers except for the native iOS browser. 
You can refer to [MSE](https://caniuse.com/?search=mse) for more information. To support the iOS browser, you can 
consider using HLS or WASM. Note that for native iOS apps, the ijkplayer can be used as a playback option.

## Usage

SRS supports HTTP-FLV distribution, you can use [docker](./getting-started.md) or [build from source](./getting-started-build.md):

```bash
docker run --rm -it -p 1935:1935 -p 8080:8080 ossrs/srs:5 \
  ./objs/srs -c conf/http.flv.live.conf
```

Use [FFmpeg(click to download)](https://ffmpeg.org/download.html) or [OBS(click to download)](https://obsproject.com/download) to push the stream:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

Open the following page to play the stream (if SRS is not on your local machine, please replace localhost with the server IP):

* HLS by SRS player: [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html)

## Config

The configuration for HTTP-FLV is as follows:

```bash
http_server {
    # whether http streaming service is enabled.
    # Overwrite by env SRS_HTTP_SERVER_ENABLED
    # default: off
    enabled on;
    # the http streaming listen entry is <[ip:]port>
    # for example, 192.168.1.100:8080
    # where the ip is optional, default to 0.0.0.0, that is 8080 equals to 0.0.0.0:8080
    # @remark, if use lower port, for instance 80, user must start srs by root.
    # Overwrite by env SRS_HTTP_SERVER_LISTEN
    # default: 8080
    listen 8080;
    # whether enable crossdomain request.
    # for both http static and stream server and apply on all vhosts.
    # Overwrite by env SRS_HTTP_SERVER_CROSSDOMAIN
    # default: on
    crossdomain on;
}
vhost __defaultVhost__ {
    # http flv/mp3/aac/ts stream vhost specified config
    http_remux {
        # whether enable the http live streaming service for vhost.
        # Overwrite by env SRS_VHOST_HTTP_REMUX_ENABLED for all vhosts.
        # default: off
        enabled on;
        # the fast cache for audio stream(mp3/aac),
        # to cache more audio and send to client in a time to make android(weixin) happy.
        # @remark the flv/ts stream ignore it
        # @remark 0 to disable fast cache for http audio stream.
        # Overwrite by env SRS_VHOST_HTTP_REMUX_FAST_CACHE for all vhosts.
        # default: 0
        fast_cache 30;
        # Whether drop packet if not match header. For example, there is has_audio and has video flag in FLV header, if
        # this is set to on and has_audio is false, then SRS will drop audio packets when got audio packets. Generally
        # it should work, but sometimes you might need SRS to keep packets even when FLV header is set to false.
        # See https://github.com/ossrs/srs/issues/939#issuecomment-1348740526
        # TODO: Only support HTTP-FLV stream right now.
        # Overwrite by env SRS_VHOST_HTTP_REMUX_DROP_IF_NOT_MATCH for all vhosts.
        # Default: on
        drop_if_not_match on;
        # Whether stream has audio track, used as default value for stream metadata, for example, FLV header contains
        # this flag. Sometimes you might want to force the metadata by disable guess_has_av.
        # For HTTP-FLV, use this as default value for FLV header audio flag. See https://github.com/ossrs/srs/issues/939#issuecomment-1351385460
        # For HTTP-TS, use this as default value for PMT table. See https://github.com/ossrs/srs/issues/939#issuecomment-1365086204
        # Overwrite by env SRS_VHOST_HTTP_REMUX_HAS_AUDIO for all vhosts.
        # Default: on
        has_audio on;
        # Whether stream has video track, used as default value for stream metadata, for example, FLV header contains
        # this flag. Sometimes you might want to force the metadata by disable guess_has_av.
        # For HTTP-FLV, use this as default value for FLV header video flag. See https://github.com/ossrs/srs/issues/939#issuecomment-1351385460
        # For HTTP-TS, use this as default value for PMT table. See https://github.com/ossrs/srs/issues/939#issuecomment-1365086204
        # Overwrite by env SRS_VHOST_HTTP_REMUX_HAS_VIDEO for all vhosts.
        # Default: on
        has_video on;
        # Whether guessing stream about audio or video track, used to generate the flags in, such as FLV header. If
        # guessing, depends on sequence header and frames in gop cache, so it might be incorrect especially your stream
        # is not regular. If not guessing, use the configured default value has_audio and has_video.
        # For HTTP-FLV, enable guessing for av header flag, because FLV can't change the header. See https://github.com/ossrs/srs/issues/939#issuecomment-1351385460
        # For HTTP-TS, ignore guessing because TS refresh the PMT when codec changed. See https://github.com/ossrs/srs/issues/939#issuecomment-1365086204
        # Overwrite by env SRS_VHOST_HTTP_REMUX_GUESS_HAS_AV for all vhosts.
        # Default: on
        guess_has_av on;
        # the stream mount for rtmp to remux to live streaming.
        # typical mount to [vhost]/[app]/[stream].flv
        # the variables:
        #       [vhost] current vhost for http live stream.
        #       [app] current app for http live stream.
        #       [stream] current stream for http live stream.
        # @remark the [vhost] is optional, used to mount at specified vhost.
        # the extension:
        #       .flv mount http live flv stream, use default gop cache.
        #       .ts mount http live ts stream, use default gop cache.
        #       .mp3 mount http live mp3 stream, ignore video and audio mp3 codec required.
        #       .aac mount http live aac stream, ignore video and audio aac codec required.
        # for example:
        #       mount to [vhost]/[app]/[stream].flv
        #           access by http://ossrs.net:8080/live/livestream.flv
        #       mount to /[app]/[stream].flv
        #           access by http://ossrs.net:8080/live/livestream.flv
        #           or by http://192.168.1.173:8080/live/livestream.flv
        #       mount to [vhost]/[app]/[stream].mp3
        #           access by http://ossrs.net:8080/live/livestream.mp3
        #       mount to [vhost]/[app]/[stream].aac
        #           access by http://ossrs.net:8080/live/livestream.aac
        #       mount to [vhost]/[app]/[stream].ts
        #           access by http://ossrs.net:8080/live/livestream.ts
        # @remark the port of http is specified by http_server section.
        # Overwrite by env SRS_VHOST_HTTP_REMUX_MOUNT for all vhosts.
        # default: [vhost]/[app]/[stream].flv
        mount [vhost]/[app]/[stream].flv;
    }
}
```

> Note: These settings are only for playing HLS. For streaming settings, please follow your protocol, like referring to [RTMP](./rtmp.md#config), [SRT](./srt.md#config), or [WebRTC](./webrtc.md#config) streaming configurations.

The important settings are explained below:

* `has_audio`: If there is an audio stream or not. If your stream doesn't have audio, set this to `off`. Otherwise, the player might wait for audio.
* `has_video`: If there is a video stream or not. If your stream doesn't have video, set this to `off`. Otherwise, the player might wait for video.

## Cluster

SRS supports HTTP-FLV cluster distribution, which can handle a large number of viewing clients. Please refer to [HTTP-FLV Cluster](./sample-http-flv-cluster.md) and [Edge](./edge.md).

## Crossdomain

SRS supports HTTP CORS by default. Please refer to [HTTP CORS](./http-server.md#crossdomain).

## Websocket FLV

You can convert HTTP-FLV to WebSocket-FLV stream. Please refer to [videojs-flow](https://github.com/winlinvip/videojs-flow).

For HTTP to WebSocket conversion, please refer to [mse.go](https://github.com/winlinvip/videojs-flow/blob/master/demo/mse.go).

## HTTP FLV VOD Stream

For HTTP FLV on-demand streaming, please refer to: [v4_CN_FlvVodStream](./flv-vod-stream.md).

## HTTP and HTTPS Proxy

SRS works well with HTTP/HTTPS proxies such as [Nginx](https://github.com/ossrs/srs/issues/2881#nginx-proxy), [HTTPX](https://github.com/ossrs/srs/issues/2881#httpx-proxy), [CaddyServer](https://github.com/ossrs/srs/issues/2881#caddy-proxy), etc. For detailed configuration, please refer to [#2881](https://github.com/ossrs/srs/issues/2881).

## HTTPS FLV Live Stream

SRS supports converting RTMP streams to HTTPS FLV streams. When publishing RTMP streams, a corresponding HTTP address is mounted in the SRS HTTP module (according to the configuration). Users can access this HTTPS FLV file, and the RTMP stream is converted to FLV for distribution.

Please refer to [HTTPS Server](./http-server.md#https-server) or the `conf/https.flv.live.conf` configuration file.

## HTTP TS Live Stream

SRS supports converting RTMP streams to HTTP TS streams. When publishing RTMP streams, a corresponding HTTP address is mounted in the SRS HTTP module (according to the configuration). Users can access this HTTP TS file, and the RTMP stream is converted to TS for distribution.

Please refer to the `conf/http.ts.live.conf` configuration file.

## HTTP Mp3 Live Stream

SRS supports discarding video from RTMP streams and converting audio streams to MP3 format. A corresponding HTTP address is mounted in the SRS HTTP module (according to the configuration). Users can access this HTTP MP3 file, and the RTMP stream is converted to MP3 for distribution.

Please refer to the `conf/http.mp3.live.conf` configuration file.

## HTTP Aac Live Stream

SRS supports discarding video from RTMP streams and converting audio streams to AAC format. A corresponding HTTP address is mounted in the SRS HTTP module (according to the configuration). Users can access this HTTP AAC file, and the RTMP stream is converted to AAC for distribution.

Please refer to the `conf/http.aac.live.conf` configuration file.

## Why HTTP FLV

Why use HTTP FLV? HTTP FLV streaming is becoming more popular. The main advantages are:

1. In the field of real-time Internet streaming media, RTMP is still dominant. HTTP-FLV has the same latency as RTMP, so it can meet latency requirements.
2. Firewall penetration: Many firewalls block RTMP but not HTTP, so HTTP FLV is less likely to have strange issues.
3. Scheduling: RTMP has a 302 feature, but it's only supported in the player's ActionScript. HTTP FLV supports 302, making it easier for CDNs to correct DNS errors.
4. Fault tolerance: SRS's HTTP FLV can have multiple sources, just like RTMP, supporting multi-level hot backup.
5. Universality: Flash can play both RTMP and HTTP FLV. Custom apps and mainstream players also support HTTP FLV playback.
6. Simplicity: FLV is the simplest streaming media encapsulation, and HTTP is the most widely used protocol. Combining these two makes maintenance much easier than RTMP.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.net&path=/lts/doc/en/v7/flv)

