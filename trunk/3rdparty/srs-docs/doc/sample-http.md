---
title: HTTP Server Deploy
sidebar_label: HTTP Server Deploy
hide_title: false
hide_table_of_contents: false
---

# SRS HTTP server deploy example

SRS embeded HTTP server, to delivery HLS and files.

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
./configure && make
```

## Step 3, config SRS

For detail, read [HLS](./hls.md) and [HTTP Server](./http-server.md)

Save bellow as config, or use `conf/http.hls.conf`:

```bash
# conf/http.hls.conf
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
}
```

Note: The hls_path must exists, srs never create it. For detail, read [HLS](./hls.md)

## Step 4, start SRS

For detail, read [HLS](./hls.md) and [SRS HTTP Server](./http-server.md)

```bash
./objs/srs -c conf/http.hls.conf
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

Or use FMLE(which support h.264+aac) to publish, read [Transcode2HLS](./sample-transcode-to-hls.md)：

```bash
FMS URL: rtmp://192.168.1.170/live
Stream: livestream
```

The streams on SRS:
* RTMP: `rtmp://192.168.1.170/live/livestream`
* HLS: `http://192.168.1.170:8080/live/livestream.m3u8`

## Step 6, play RTMP

For detail, read [HLS](./hls.md)

RTMP url is: `rtmp://192.168.1.170:1935/live/livestream`

User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

## Step 7, play HLS

For detail, read [HLS](./hls.md)

HLS url: `http://192.168.1.170:8080/live/livestream.m3u8`

User can use vlc to play the HLS stream.

Or, use online SRS player: [srs-player](https://ossrs.net/players/srs_player.html)

Note: Please replace all ip 192.168.1.170 to your server ip.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-http)


