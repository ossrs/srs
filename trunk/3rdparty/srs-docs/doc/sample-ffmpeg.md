---
title: Transcode Deploy
sidebar_label: Transcode Deploy
hide_title: false
hide_table_of_contents: false
---

# Transcode deploy example

FFMPEG can used to transcode the live stream, output the other RTMP server.
For detail, read [FFMPEG](./ffmpeg.md).

**Suppose the server ip is 192.168.1.170**

## Step 1, get SRS

For detail, read [GIT](./git.md)

```bash
git clone https://github.com/ossrs/srs
cd srs/trunk
```

Or update the exists code:

```bash
git pull
```

## Step 2, build SRS

For detail, read [Build](./install.md)

```bash
./configure --ffmpeg-tool=on && make
```

## Step 3, config file

For detail, read [FFMPEG](./ffmpeg.md)

Save the bellow as config file, or use `conf/ffmpeg.transcode.conf` instead:

```bash
# conf/ffmpeg.transcode.conf
listen              1935;
max_connections     1000;
vhost __defaultVhost__ {
    transcode {
        enabled     on;
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine ff {
            enabled         on;
            vfilter {
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

## Step 4, start SRS

For detail, read [FFMPEG](./ffmpeg.md)

```bash
./objs/srs -c conf/ffmpeg.conf
```

## Step 5, start encoder

For detail, read [FFMPEG](./ffmpeg.md)

Use FFMPEG to publish stream:

```bash
    for((;;)); do \
        ./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.flv \
        -c copy \
        -f flv rtmp://192.168.1.170/live/livestream; \
        sleep 1; \
    done
```

Or use FMLE to publish:

```bash
FMS URL: rtmp://192.168.1.170/live
Stream: livestream
```

The stream in SRS:
* Stream publish by encoder: rtmp://192.168.1.170:1935/live/livestream
* Play the original stream: rtmp://192.168.1.170:1935/live/livestream
* Play the transcoded stream: rtmp://192.168.1.170:1935/live/livestream_ff

## Step 6, play the stream

For detail, read [FFMPEG](./ffmpeg.md)

RTMP url is: `rtmp://192.168.1.170:1935/live/livestream`

User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

## Step 7, play the transcoded stream

For detail, read [FFMPEG](./ffmpeg.md)

RTMP url is: `rtmp://192.168.1.170:1935/live/livestream_ff`

User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-ffmpeg)


