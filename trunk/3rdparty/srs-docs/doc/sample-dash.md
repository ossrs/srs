---
title: DASH Deploy
sidebar_label: DASH Deploy
hide_title: false
hide_table_of_contents: false
---

# DASH deploy example

Delivery DASH by SRS:

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

Please read [DASH](https://github.com/ossrs/srs/issues/299#issuecomment-306022840)

Save bellow as config, or use `conf/dash.conf`:

```bash
# conf/dash.conf
listen              1935;
max_connections     1000;
daemon              off;
srs_log_tank        console;
http_server {
    enabled         on;
    listen          8080;
    dir             ./objs/nginx/html;
}
vhost __defaultVhost__ {
    dash {
        enabled         on;
        dash_fragment       30;
        dash_update_period  150;
        dash_timeshift      300;
        dash_path           ./objs/nginx/html;
        dash_mpd_file       [app]/[stream].mpd;
    }
}
```

## Step 4, start SRS

```bash
./objs/srs -c conf/dash.conf
```

> Note: You can also use other web server, such as NGINX, to delivery files of DASH.

## Step 5, start Encoder

Use FFMPEG to publish stream:

```bash
    for((;;)); do \
        ./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.flv \
        -c copy \
        -f flv rtmp://192.168.1.170/live/livestream; \
        sleep 1; \
    done
```

The stream in SRS:
* RTMP url：`rtmp://192.168.1.170/live/livestream`
* DASH url： `http://192.168.1.170:8080/live/livestream.mpd`

## Step 6, play RTMP stream

RTMP url is: `rtmp://192.168.1.170:1935/live/livestream`

User can use vlc to play the RTMP stream.

Note: Please replace all ip 192.168.1.170 to your server ip.

## Step 7, play DASH stream

DASH url： `http://192.168.1.170:8080/live/livestream.mpd`

Please use VLC to play.

Winlin 2020.01

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-dash)


