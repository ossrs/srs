---
title: HLS Cluster Deploy
sidebar_label: HLS Cluster Deploy
hide_title: false
hide_table_of_contents: false
---

# HLS Edge Cluster Example

Example for HLS Edge Cluster, like to create a CDN to deliver HLS files.

**Suppose the server ip is 192.168.1.170**

## Step 1, Get SRS code

For detail, read [GIT](./git.md)

```bash
git clone https://github.com/ossrs/srs
cd srs/trunk
```

Or update the exists code:

```bash
git pull
```

## Step 2, Configure and build SRS

For detail, read [Build](./install.md)

```bash
./configure && make
```

## Step 3, Config origin srs, to generate HLS files

See [HLS](./hls.md).

Please use config `conf/hls.origin.conf`, or create a config file by:

```bash
# conf/hls.origin.conf
listen              1935;
max_connections     1000;
daemon              off;
srs_log_tank        console;
http_server {
    enabled         on;
    listen          8080;
}
vhost __defaultVhost__ {
    hls {
        enabled         on;
        hls_ctx off;
        hls_ts_ctx off;
    }
}
```

## Step 4, Config edge NGINX to deliver HLS files.

See [Nginx for HLS](./nginx-for-hls.md).

Save bellow as config, or use `conf/hls.edge.conf`:

```bash
# conf/hls.edge.conf
worker_processes  3;
events {
    worker_connections  10240;
}

http {
    # For Proxy Cache.
    proxy_cache_path  /tmp/nginx-cache levels=1:2 keys_zone=srs_cache:8m max_size=1000m inactive=600m;
    proxy_temp_path /tmp/nginx-cache/tmp; 

    server {
        listen       8081;
        # For Proxy Cache.
        proxy_cache_valid  404      10s;
        proxy_cache_lock on;
        proxy_cache_lock_age 300s;
        proxy_cache_lock_timeout 300s;
        proxy_cache_min_uses 1;

        location ~ /.+/.*\.(m3u8)$ {
            proxy_pass http://127.0.0.1:8080$request_uri;
            # For Proxy Cache.
            proxy_cache srs_cache;
            proxy_cache_key $scheme$proxy_host$uri$args;
            proxy_cache_valid  200 302  10s;
        }
        location ~ /.+/.*\.(ts)$ {
            proxy_pass http://127.0.0.1:8080$request_uri;
            # For Proxy Cache.
            proxy_cache srs_cache;
            proxy_cache_key $scheme$proxy_host$uri;
            proxy_cache_valid  200 302  60m;
        }
    }
}
```

## Step 5, Start SRS Origin and NGINX Edge Server

```bash
nginx -c $(pwd)/conf/hls.edge.conf
./objs/srs -c conf/hls.origin.conf
```

> Note: Please follow instructions of [NGINX](https://nginx.org/) to download and install.

## Step 6, Publish RTMP stream to SRS Origin, to generate HLS files.

Use FFMPEG to publish stream:

```bash
for((;;)); do \
    ./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.flv \
    -c copy -f flv rtmp://192.168.1.170/live/livestream; \
    sleep 1; \
done
```

Or use OBS to publish:

```bash
Server: rtmp://192.168.1.170/live
StreamKey: livestream
```

## Step 7, Play HLS stream

HLS by SRS Origin: `http://192.168.1.170:8080/live/livestream.m3u8`

HLS by NGINX Edge: `http://192.168.1.170:8081/live/livestream.m3u8`

Note: Please replace all ip 192.168.1.170 to your server ip.

## Step 8: Benchmark and More NGINX Edge Servers

Please use [srs-bench](https://github.com/ossrs/srs-bench#usage) to simulate a set of visitors:

```bash
docker run --rm -it --network=host --name sb ossrs/srs:sb \
  ./objs/sb_hls_load -c 100 -r http://192.168.1.170:8081/live/livestream.m3u8
```

You could run more NGINX from another server, use the same config.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample-hls-cluster)


