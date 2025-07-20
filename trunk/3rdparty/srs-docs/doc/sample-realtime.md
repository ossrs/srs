---
title: RTMP Realtime Deploy
sidebar_label: RTMP Realtime Deploy
hide_title: false
hide_table_of_contents: false
---

# RTMP low latency deploy example

The SRS realtime(low latency) mode can decrease the latency to 0.8-3s.
For detail about latency, read [LowLatency](./low-latency.md).

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

For detail, read [LowLatency](./low-latency.md)

Save bellow as config, or use `conf/realtime.conf`:

```bash
# conf/realtime.conf
listen              1935;
max_connections     1000;
vhost __defaultVhost__ {
    tcp_nodelay     on;
    min_latency     on;

    play {
        gop_cache       off;
        queue_length    10;
        mw_latency      100;
    }

    publish {
        mr off;
    }
}
```

## Step 4, start SRS

For detail, read [LowLatency](./low-latency.md)

```bash
./objs/srs -c conf/realtime.conf
```

## Step 5, start Encoder

For detail, read [LowLatency](./low-latency.md)

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

Note: To measure the latency, can use the clock of mobile phone.
![latency](/img/sample-realtime-001.png)

## Step 6, play RTMP

For detail, read [LowLatency](./low-latency.md)

RTMP url is: `rtmp://192.168.1.170:1935/live/livestream`

User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

Winlin 2014.12

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-realtime)


