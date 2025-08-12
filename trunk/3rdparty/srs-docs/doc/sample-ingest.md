---
title: Ingest Deploy
sidebar_label: Ingest Deploy
hide_title: false
hide_table_of_contents: false
---

# Ingest deploy example

SRS can start process to ingest file/stream/device, transcode or not,
then publish to SRS. For detail, read [Ingest](./ingest.md).

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

For detail, read [Ingest](./ingest.md)

Save bellow as config, or use `conf/ingest.conf`:

```bash
# conf/ingest.conf
listen              1935;
max_connections     1000;
vhost __defaultVhost__ {
    ingest livestream {
        enabled      on;
        input {
            type    file;
            url     ./doc/source.flv;
        }
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine {
            enabled          off;
            output          rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream;
        }
    }
}
```

## Step 4, start SRS

For detail, read [Ingest](./ingest.md)

```bash
./objs/srs -c conf/ingest.conf
```

The streams on SRS:
* Stream ingest: rtmp://192.168.1.170:1935/live/livestream

## Step 5, play RTMP

For detail, read [Ingest](./ingest.md)

RTMP url is: `rtmp://192.168.1.170:1935/live/livestream`

User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-ingest)


