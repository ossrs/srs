---
title: FFMPEG
sidebar_label: FFMPEG 
hide_title: false
hide_table_of_contents: false
---

# Live Streaming Transcode

SRS can transcode RTMP streams and output to any RTMP server, typically itself.

## Use Scenario

The important use scenario of FFMPEG:
* One in N out: Publish a high resolution video with big bitrate, for intance, h.264 5Mbps 1080p. Then use FFMPEG to transcode to multiple bitrates, for example, 1080p/720p/576p, the 576p is for mobile devices.
* Support multiple screen: The stream published by flash is in h264/vp6/mp3/speex codec. Use FFMPEG to transcode to HLS(h264+aac) for IOS/Android.
* Stream filters: For example, add logo to stream. SRS supports all filters from FFMPEG.
* Snapshot: Please read [snapshot by transcoder](./snapshot.md#transcoder)

## Workflow

The workflow of SRS transcoding:

1. Encoder publishes RTMP to SRS.
1. SRS forks a process for FFMPEG when transcoding is configured.
1. The forked FFMPEG transcodes the stream and publishes it to SRS or other servers.

## Transcode Config

The SRS transcoding feature can apply on vhost, app or a specified stream.

```bash
listen              1935;
vhost __defaultVhost__ {
    # the streaming transcode configs.
    transcode {
        # whether the transcode enabled.
        # if off, donot transcode.
        # default: off.
        enabled     on;
        # the ffmpeg 
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        # the transcode engine for matched stream.
        # all matched stream will transcoded to the following stream.
        # the transcode set name(ie. hd) is optional and not used.
        engine example {
            # whether the engine is enabled
            # default: off.
            enabled         on;
            # input format, can be:
            # off, do not specifies the format, ffmpeg will guess it.
            # flv, for flv or RTMP stream.
            # other format, for example, mp4/aac whatever.
            # default: flv
            iformat         flv;
            # ffmpeg filters, follows the main input.
            vfilter {
                # the logo input file.
                i               ./doc/ffmpeg-logo.png;
                # the ffmpeg complex filter.
                # for filters, @see: http://ffmpeg.org/ffmpeg-filters.html
                filter_complex  'overlay=10:10';
            }
            # video encoder name. can be:
            #       libx264: use h.264(libx264) video encoder.
            #       png: use png to snapshot thumbnail.
            #       copy: donot encoder the video stream, copy it.
            #       vn: disable video output.
            vcodec          libx264;
            # video bitrate, in kbps
            # @remark 0 to use source video bitrate.
            # default: 0
            vbitrate        1500;
            # video framerate.
            # @remark 0 to use source video fps.
            # default: 0
            vfps            25;
            # video width, must be even numbers.
            # @remark 0 to use source video width.
            # default: 0
            vwidth          768;
            # video height, must be even numbers.
            # @remark 0 to use source video height.
            # default: 0
            vheight         320;
            # the max threads for ffmpeg to used.
            # default: 1
            vthreads        12;
            # x264 profile, @see x264 -help, can be:
            # high,main,baseline
            vprofile        main;
            # x264 preset, @see x264 -help, can be: 
            #       ultrafast,superfast,veryfast,faster,fast
            #       medium,slow,slower,veryslow,placebo
            vpreset         medium;
            # other x264 or ffmpeg video params
            vparams {
                # ffmpeg options, @see: http://ffmpeg.org/ffmpeg.html
                t               100;
                # 264 params, @see: http://ffmpeg.org/ffmpeg-codecs.html#libx264
                coder           1;
                b_strategy      2;
                bf              3;
                refs            10;
            }
            # audio encoder name. can be:
            #       libfdk_aac: use aac(libfdk_aac) audio encoder.
            #       copy: donot encoder the audio stream, copy it.
            #       an: disable audio output.
            acodec          libfdk_aac;
            # audio bitrate, in kbps. [16, 72] for libfdk_aac.
            # @remark 0 to use source audio bitrate.
            # default: 0
            abitrate        70;
            # audio sample rate. for flv/rtmp, it must be:
            #       44100,22050,11025,5512
            # @remark 0 to use source audio sample rate.
            # default: 0
            asample_rate    44100;
            # audio channel, 1 for mono, 2 for stereo.
            # @remark 0 to use source audio channels.
            # default: 0
            achannels       2;
            # other ffmpeg audio params
            aparams {
                # audio params, @see: http://ffmpeg.org/ffmpeg-codecs.html#Audio-Encoders
                # @remark SRS supported aac profile for HLS is: aac_low, aac_he, aac_he_v2
                profile:a   aac_low;
                bsf:a       aac_adtstoasc;
            }
            # output format, can be:
            #       off, do not specifies the format, ffmpeg will guess it.
            #       flv, for flv or RTMP stream.
            #       image2, for vcodec png to snapshot thumbnail.
            #       other format, for example, mp4/aac whatever.
            # default: flv
            oformat         flv;
            # output stream. variables:
            #       [vhost] the input stream vhost.
            #       [port] the intput stream port.
            #       [app] the input stream app.
            #       [stream] the input stream name.
            #       [engine] the tanscode engine name.
            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];
        }
    }
}
```

The configuration applies to all streams of this vhost, for example:
* Publish stream to: rtmp://dev:1935/live/livestream
* Play the origin stream: rtmp://dev:1935/live/livestream
* Play the transcoded stream: rtmp://dev:1935/live/livestream_ff

The output URL contains some variables:
* [vhost] The input stream vhost, for instance, dev.ossrs.net
* [port] The input stream port, for instance, 1935
* [app] The input stream app, for instance, live
* [stream] The input stream name, for instance, livestream
* [engine] The transcode engine name, which follows the keyword engine, for instance, ff

Add the app or app/stream when you need to apply transcoding to it:

```bash
listen              1935;
vhost __defaultVhost__ {
    # Transcode all streams of app "live"
    transcode live {
    }
}
```

Or for streams:

```bash
listen              1935;
vhost __defaultVhost__ {
    # Transcode stream name is "livestream" and app is "live"
    transcode live/livestream{
    }
}
```

## Transcode Rulers

All params of SRS transcode are for FFMPEG, and SRS renames some parameters:

| SRS | FFMPEG | Exammple | Description |
| ------ | --------- | ---- | ----- |
| vcodec | vcodec | ffmpeg ... -vcodec libx264 ... | The codec to use. |
| vbitrate | b:v | ffmpeg ... -b:v 500000 ... | The bitrate in kbps (for SRS) or bps (for FFMPEG) at which to output the transcoded stream. |
| vfps | r | ffmpeg ... -r 25 ... | The output framerate. |
| vwidth/vheight | s | ffmpeg ... -s 400x300 -aspect 400:300 ... | The output video size, the width x height and the aspect set to width:height. |
| vthreads | threads | ffmpeg ... -threads 8 ... | The number of encoding threads for x264. |
| vprofile | profile:v | ffmpeg ... -profile:v high ... | The profile for x264. |
| vpreset | preset | ffmpeg ... -preset medium ... | The preset for x264. |
| acodec | acodec | ffmpeg ... -acodec libfdk_aac ... | The codec for audio. |
| abitrate | b:a | ffmpeg ... -b:a 70000 ... | The bitrate in kbps (for SRS) and bps (for FFMPEG) for output audio. For libaacplus：16-72k. No limit for libfdk_aac. |
| asample_rate | ar | ffmpeg ... -ar 44100 ... | The audio sample rate. |
| achannels | ac | ffmpeg ... -ac 2 ... | The audio channel. |

There are more parameters for SRS:
* vfilter：Parameters added before the vcodec, for the FFMPEG filters.
* vparams：Parameters added after the vcodec, for the video transcode parameters.
* aparams：Parameters added after the acodec and before the -y, for the audio transcode parameters.

These parameters will generated by the sequence:

```bash
ffmpeg -f flv -i <input_rtmp> {vfilter} -vcodec ... {vparams} -acodec ... {aparams} -f flv -y {output}
```

The actual parameters used to fork FFMPEG can be found in the log by the keywords `start transcoder`:

```bash
[2014-02-28 21:38:09.603][4][trace][start] start transcoder, 
log: ./objs/logs/encoder-__defaultVhost__-live-livestream.log, 
params: ./objs/ffmpeg/bin/ffmpeg -f flv -i 
rtmp://127.0.0.1:1935/live?vhost=__defaultVhost__/livestream 
-vcodec libx264 -b:v 500000 -r 25.00 -s 768x320 -aspect 768:320 
-threads 12 -profile:v main -preset medium -acodec libfdk_aac 
-b:a 70000 -ar 44100 -ac 2 -f flv 
-y rtmp://127.0.0.1:1935/live?vhost=__defaultVhost__/livestream_ff 
```

## FFMPEG Log Path

When an FFMPEG process is forked, SRS will redirect the stdout and stderr to the log file, for instance, `./objs/logs/encoder-__defaultVhost__-live-livestream.log`. Sometimes the log file is very large, so users can add parameters to vfilter to tell FFMPEG to generate less verbose logs:

```bash
listen              1935;
vhost __defaultVhost__ {
    transcode {
        enabled     on;
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine ff {
            enabled         on;
            vfilter {
                # -v quiet
                v           quiet;
            }
            vcodec          libx264;
            vbitrate        500;
            vfps            25;
            vwidth          768;
            vheight         320;
            vthreads        12;
            vprofile        main;
            vpreset         medium;
            vparams {
            }
            acodec          libfdk_aac;
            abitrate        70;
            asample_rate    44100;
            achannels       2;
            aparams {
            }
            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];
        }
    }
}
```

That is, add the parameter `-v quiet` to FFMPEG.

## Copy Without Transcode

Set the vcodec/acodec to copy, FFMPEG will demux and mux without transcoding, like the forward of SRS. Users can copy video and transcode audio, for example, when flash is publishing the stream with h264+speex.

```bash
listen              1935;
vhost __defaultVhost__ {
    transcode {
        enabled     on;
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine ff {
            enabled         on;
            vcodec          copy;
            acodec          libfdk_aac;
            abitrate        70;
            asample_rate    44100;
            achannels       2;
            aparams {
            }
            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];
        }
    }
}
```

Or, copy video and audio:
```bash
listen              1935;
vhost __defaultVhost__ {
    transcode {
        enabled     on;
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine ff {
            enabled         on;
            vcodec          copy;
            acodec          copy;
            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];
        }
    }
}
```

## Drop Video or Audio

FFMPEG can drop video or audio streams by configuring vcodec to vn and acodec to an. For example:

```bash
listen              1935;
vhost __defaultVhost__ {
    transcode {
        enabled     on;
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine vn {
            enabled         on;
            vcodec          vn;
            acodec          libfdk_aac;
            abitrate        45;
            asample_rate    44100;
            achannels       2;
            aparams {
            }
            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];
        }
    }
}
```

The configuration above will output pure audio in the aac codec.

## Other Transcoding Configuration

There are lots of vhost in conf/full.conf for transcoding, or refer to FFMPEG:
* mirror.transcode.srs.com
* drawtext.transcode.srs.com 
* crop.transcode.srs.com
* logo.transcode.srs.com 
* audio.transcode.srs.com
* copy.transcode.srs.com
* all.transcode.srs.com
* ffempty.transcode.srs.com 
* app.transcode.srs.com 
* stream.transcode.srs.com 
* vn.transcode.srs.com

## FFMPEG Transcoding Streams by Flash Encoder

Flash web pages can encode and publish RTMP streams to the server, and the audio codec must be speex, nellymoser or pcma/pcmu.

Flash will disable audio when no audio is published, so FFMPEG may cannot discover the audio in the stream and will disable the audio.

## FFMPEG

FFMPEG links:
* [ffmpeg.org](http://ffmpeg.org)
* [ffmpeg CLI](http://ffmpeg.org/ffmpeg.html)
* [ffmpeg filters](http://ffmpeg.org/ffmpeg-filters.html)
* [ffmpeg codecs](http://ffmpeg.org/ffmpeg-codecs.html)

Winlin 2015.6

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/ffmpeg)


