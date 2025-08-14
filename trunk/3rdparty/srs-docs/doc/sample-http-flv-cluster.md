---
title: HTTP-FLV Cluster Deploy
sidebar_label: HTTP-FLV Cluster Deploy
hide_title: false
hide_table_of_contents: false
---

# HTTP FLV Cluster Example

About the HTTP FLV cluster of SRS, read [HTTP FLV](./flv.md#about-http-flv)

How to use multiple process for HTTP FLV? Please read [Reuse Port](./reuse-port.md) for detail.

This example show how to deploy three SRS instance, listen at different port at a machine(user can deploy each to different machine, use same port), while one is origin server, another two are edge servers. We can publish RTMP to origin or edge, and play the RTMP/FLV at any edge. The latency is same to RTMP, 0.8-1s.

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

## Step 3, config origin SRS

For detail, read [HTTP FLV](./flv.md)

Save bellow as config, or use `conf/http.flv.live.conf`:

```bash
# conf/http.flv.live.conf
listen              1935;
max_connections     1000;
http_server {
    enabled         on;
    listen          8080;
    dir             ./objs/nginx/html;
}
vhost __defaultVhost__ {
    http_remux {
        enabled     on;
        mount       [vhost]/[app]/[stream].flv;
        hstrs       on;
    }
}
```

## Step 4, config edge SRS

For detail, read [HTTP FLV](./flv.md)

Save bellow as config, or use `conf/http.flv.live.edge1.conf` or `conf/http.flv.live.edge2.conf`:

```bash
# conf/http.flv.live.edge1.conf
listen              19351;
max_connections     1000;
pid                 objs/srs.flv.19351.pid;
srs_log_file        objs/srs.flv.19351.log;
http_server {
    enabled         on;
    listen          8081;
    dir             ./objs/nginx/html;
}
vhost __defaultVhost__ {
    mode remote;
    origin 127.0.0.1;
    http_remux {
        enabled     on;
        mount       [vhost]/[app]/[stream].flv;
        hstrs       on;
    }
}
```

## Step 5, start SRS

For detail, read [HTTP FLV](./flv.md)

```bash
./objs/srs -c conf/http.flv.live.conf &
./objs/srs -c conf/http.flv.live.edge1.conf &
./objs/srs -c conf/http.flv.live.edge2.conf &
```

## Step 6, start Encoder

For detail, read read [HTTP FLV](./flv.md)

Use FFMPEG to publish stream:

```bash
    for((;;)); do \
        ./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.flv \
        -c copy \
        -f flv rtmp://192.168.1.170/live/livestream; \
        sleep 1; \
    done
```

Or use FMLE to publish：

```bash
FMS URL: rtmp://192.168.1.170/live
Stream: livestream
```

The streams on SRS origin:
* RTMP: `rtmp://192.168.1.170/live/livestream`
* HTTP FLV: `http://192.168.1.170:8080/live/livestream.flv`

The streams on SRS edge1:
* RTMP: `rtmp://192.168.1.170:19351/live/livestream`
* HTTP FLV: `http://192.168.1.170:8081/live/livestream.flv`

The streams on SRS edge2:
* RTMP: `rtmp://192.168.1.170:19352/live/livestream`
* HTTP FLV: `http://192.168.1.170:8082/live/livestream.flv`

## Step 7, play RTMP

For detail, read [HTTP FLV](./flv.md)

Origin RTMP url is: `rtmp://192.168.1.170:1935/live/livestream`, User can use vlc to play the RTMP stream.

Edge1 RTMP url is: `rtmp://192.168.1.170:19351/live/livestream`, User can use vlc to play the RTMP stream.

Edge2 RTMP url is: `rtmp://192.168.1.170:19352/live/livestream`, User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

## Step 8, play HTTP FLV

For detail, read [HTTP FLV](./flv.md)

Origin HTTP FLV url: `http://192.168.1.170:8080/live/livestream.flv`, User can use vlc to play the HLS stream. Or, use online SRS player(you must input the flv url): [srs-player](https://ossrs.net/players/srs_player.html)

Edge1 HTTP FLV url: `http://192.168.1.170:8081/live/livestream.flv`, User can use vlc to play the HLS stream. Or, use online SRS player(you must input the flv url): [srs-player](https://ossrs.net/players/srs_player.html)

Edge2 HTTP FLV url: `http://192.168.1.170:8082/live/livestream.flv`, User can use vlc to play the HLS stream. Or, use online SRS player(you must input the flv url): [srs-player](https://ossrs.net/players/srs_player.html)

Note: Please replace all ip 192.168.1.170 to your server ip.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-http-flv-cluster)


