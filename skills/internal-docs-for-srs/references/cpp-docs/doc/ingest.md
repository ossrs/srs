---
title: Ingest
sidebar_label: Ingest 
hide_title: false
hide_table_of_contents: false
---

# Ingest

Ingest is used to ingest file(flv, mp4, mkv, avi, rmvb...), 
stream(RTMP, RTMPT, RTMPS, RTSP, HTTP, HLS...) and device,
encode or passthrough then publish as RTMP to SRS.

Ingest actually use FFmpeg, or your tool, to encode or remux
to suck known data to RTMP to SRS.

How to deploy ingest, read [Ingest](./sample-ingest.md)

## Use Scenario

The main use scenarios:
* Virtual Live Stream: Convert vod file to live stream.
* Input RTSP IP Camera: Many IP Camera supports to pull in RTSP, user can ingest the RTSP to RTMP to SRS.
* Directly ingest device, use the FFmpeg as encoder actually.
* Ingest HTTp stream to RTMP for some old stream server.

In a word, the Ingest is used to ingest any stream supported by FFMPEG to SRS.

SRS server is support encoder to publish stream, while ingest can enable SRS to act like a client to pull 
stream from other place.

## Build

Config SRS with option `--with-ingest`, read [Build](./install.md)

The ingest tool of SRS can use FFMPEG, or use your own tool.

## Config

The config to use ingest:

```bash
vhost your_vhost {
    # ingest file/stream/device then push to SRS over RTMP.
    # the name/id used to identify the ingest, must be unique in global.
    # ingest id is used in reload or http api management.
    ingest livestream {
        # whether enabled ingest features
        # default: off
        enabled      on;
        # input file/stream/device
        # @remark only support one input.
        input {
            # the type of input.
            # can be file/stream/device, that is,
            #   file: ingest file specifies by url.
            #   stream: ingest stream specifeis by url.
            #   device: not support yet.
            # default: file
            type    file;
            # the url of file/stream.
            url     ./doc/source.flv;
        }
        # the ffmpeg 
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        # the transcode engine, @see all.transcode.srs.com
        # @remark, the output is specified following.
        engine {
            # @see enabled of transcode engine.
            # if disabled or vcodec/acodec not specified, use copy.
            # default: off.
            enabled          off;
            # output stream. variables:
            # [vhost] current vhost which start the ingest.
            # [port] system RTMP stream port.
            output          rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream;
        }
    }
}
```

The word after ingest keyword is the id of ingest, the id must be unique.

The `type` specifies the ingest type:
* file: To ingest file to RTMP, SRS will add `-re` for FFMPEG.
* stream: To ingest stream to RTMP.
* device: Not support yet.

The `engine` specifies the transcode engine and output:
* enabled: Whether transcode, remux when off.
* output：The output RTMP url. The vhost and port is variable.
* others is same to [FFMPEG](./ffmpeg.md)

Note: Engine is copy, when:
* The enabled is off.
* The vcodec and acodec is not specified.

## Ingest File list

SRS does not ingest a file list, a wordaround:
* Use script as the ingest tool, which use ffmpeg to copy file to RTMP stream one by one.

Read https://github.com/ossrs/srs/issues/55

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/ingest)


