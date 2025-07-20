---
title: Transcode HLS Deploy
sidebar_label: Transcode HLS Deploy
hide_title: false
hide_table_of_contents: false
---

# Transcode for HLS deploy example

HLS required h.264+aac, user can transcode for other codecs.

Pure audio HLS, read [HLS audio only][http://ossrs.net/srs.release/wiki/HLS-Audio-Only]

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

## Step 3, config SRS

For detail, read [HLS](./hls.md)

Save bellow as config, or use `conf/transcode2hls.audio.only.conf`:

```bash
# conf/transcode2hls.audio.only.conf
listen              1935;
max_connections     1000;
http_server {
    enabled         on;
    listen          8080;
    dir             ./objs/nginx/html;
}
vhost __defaultVhost__ {
    hls {
        enabled         on;
        hls_path        ./objs/nginx/html;
        hls_fragment    10;
        hls_window      60;
    }
    transcode {
        enabled     on;
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine ff {
            enabled         on;
            vcodec          copy;
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

## Step 4, strat SRS

For detail, read [HLS](./hls.md)

```bash
./objs/srs -c conf/transcode2hls.audio.only.conf
```

## Step 5, start Encoder

For detail, read [HLS](./hls.md)

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
* RTMP URL: `rtmp://192.168.1.170/live/livestream`
* Transcode RTMP: `rtmp://192.168.1.170/live/livestream_ff`
* Transcode HLS: `http://192.168.1.170:8080/live/livestream_ff.m3u8`

Note: we can use another vhost to output HLS, other codecs transcode then output to this vhost.

## Step 6, play RTMP

For detail, read [HLS](./hls.md)

RTMP url is: `rtmp://192.168.1.170:1935/live/livestream_ff`

User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

## Step 7, play HLS

For detail, read [HLS](./hls.md)

HLS url: `http://192.168.1.170:8080/live/livestream_ff.m3u8`

User can use vlc to play the HLS stream.

Or, use online SRS player: [srs-player](https://ossrs.net/players/srs_player.html)

Note: Please replace all ip 192.168.1.170 to your server ip.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-transcode-to-hls)


