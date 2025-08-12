---
title: FLV Vod Streaming
sidebar_label: FLV Vod Streaming
hide_title: false
hide_table_of_contents: false
---

# FLV vod streaming

## HTTP VOD

I recomment:

* Vod stream should always use HTTP protocol, never use RTMP.
SRS can dvr RTMP live stream to flv file, and provides some tools for vod stream,
but user should use other HTTP server to delivery flv file as vod stream.
* In a word, SRS does not support vod, only support live.

The workflow of flv vod stream:

* SRS dvr live stream to flv file, or upload flv vod file, to the HTTP root dir: `objs/nginx/html`
* HTTP server must support flv?start=offset, for example, flv module of nginx, or use experiment SRS HTTP server.
* Use `research/librtmp/objs/srs_flv_injecter` inject the keyframe offset to metadata of flv.
* Flash player play http flv url, for instance, `http://192.168.1.170:8080/sample.flv`
* When user seek, for instance, seek to 300s.
* Player use the keyframe offset in metadata to calc the offset of 300s, for instance, 300s offset=`6638860`
* Start new request, url is `http://192.168.1.170:8080/sample.flv?start=6638860`

Note: SRS HTTP server is experiment, do not limit the bandwidth.
Note: SRS provides flv view tool `research/librtmp/objs/srs_flv_parser`, to list the seconds:offsets in metadata.

## SRS Embeded HTTP server

SRS supports http-api, so SRS can also parse HTTP protocol(partial HTTP right now), 
so SRS also implements a experiment HTTP server.

SRS HTTP server is rewrite, table and partial HTTP protocol support, 
ok for online service.

For some emebeded device, for instance, arm linux, user can use SRS HTTP server,
for arm is not easy to build some server.

## Config

Read [HTTP Server](./http-server.md#config)

Winlin 2015.1

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/flv-vod-stream)


