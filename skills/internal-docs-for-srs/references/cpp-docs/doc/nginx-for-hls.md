---
title: HLS Cluster
sidebar_label: HLS Cluster
hide_title: false
hide_table_of_contents: false
---

# Nginx for HLS

Edge Cluster is designed to solve the problem of many people watching, and it can support a large number of people watching live streams. Please note:

* SRS Edge only supports live streaming protocols, such as RTMP or HTTP-FLV, etc. Refer to [RTMP Edge Cluster](./sample-rtmp-cluster.md).
* SRS Edge does not support sliced live streams like HLS or DASH. Essentially, they are not streams but file distribution.
* SRS Edge does not support WebRTC stream distribution, as this is not the design goal of Edge. WebRTC has its own clustering method, refer to [#2091](https://github.com/ossrs/srs/issues/2091).

This article describes the edge cluster for HLS or DASH slices, which is based on NGINX implementation, so it is also called NGINX Edge Cluster.

## Oryx

The NGINX edge cluster can work together with the Oryx to achieve HLS distribution. For more details, please refer to [Oryx HLS CDN](https://github.com/ossrs/oryx/tree/main/scripts/nginx-hls-cdn).

## NGINX Edge Cluster

The NGINX edge cluster is essentially a reverse proxy with caching, also known as NGINX Proxy with Cache.

```text
+------------+          +------------+          +------------+          +------------+
+ FFmpeg/OBS +--RTMP-->-+ SRS Origin +--HLS-->--+ NGINX      +--HLS-->--+ Visitors   +
+------------+          +------------+          + Servers    +          +------------+
                                                +------------+          
```

You only need to configure the caching strategy of NGINX, no additional plugins are needed, as NGINX itself supports it.

```bash
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

> Note: You can configure the cache directory `proxy_cache_path` and `proxy_temp_path` to be accessible directories.

> Note: Generally, do not modify the `location` configuration unless you know what it means. If you want to change it, make sure it runs first before making changes.

You must not configure it as a pure Proxy, as this will pass the load through to SRS, and the number of clients the system supports will still be limited by SRS.

After enabling Cache, no matter how much load NGINX has, SRS will only have one stream. In this way, we can expand multiple NGINX to support a large number of concurrent viewers.

For example, a 1Mbps HLS stream, with 1000 clients playing on NGINX, the bandwidth of NGINX would be 1Gbps, while SRS would only have 1Mbps.

If we expand to 10 NGINX, each with 10Gbps bandwidth, the total system bandwidth would be 100Gbps, capable of supporting 100,000 concurrent viewers, with SRS bandwidth consumption only at 10Mbps.

How to verify that the system is working properly? This is where Benchmark comes in.

## Benchmark

How to stress test this system? You can use [srs-bench](https://github.com/ossrs/srs-bench#usage), which is very convenient to use and can be started directly with Docker:

```bash
docker run --rm -it --network=host --name sb ossrs/srs:sb \
  ./objs/sb_hls_load -c 500 \
  -r http://your_server_public_ipv4/live/livestream.m3u8
```

And you can also stress test RTMP and HTTP-FLV:

```bash
docker run --rm -it --network=host --name sb ossrs/srs:sb \
  ./objs/sb_http_load -c 500 \
  -r http://your_server_public_ipv4/live/livestream.flv
```

> Note: Each SB simulated client concurrency is between 500 and 1000, depending on the CPU not exceeding 80%. You can start multiple processes for stress testing.

Now let's get our hands on creating an HLS cluster.

## Example

Now let's use Docker to build an HLS distribution cluster.

First, start the SRS origin server:

```bash
./objs/srs -c conf/hls.origin.conf
```

Then, start the NGINX origin server:

```bash
nginx -c $(pwd)/conf/hls.edge.conf
```

Finally, push the stream to the origin server:

```bash
ffmpeg -re -i doc/source.flv -c copy \
  -f flv rtmp://127.0.0.1/live/livestream
```

Play HLS:

* SRS origin server: http://127.0.0.1:8080/live/livestream.m3u8
* NGINX edge: http://127.0.0.1:8081/live/livestream.m3u8

Start the stress test and get HLS from NGINX:

```bash
docker run --rm -it --network=host --name sb ossrs/srs:sb \
  ./objs/sb_hls_load -c 500 \
  -r http://192.168.0.14:8081/live/livestream.m3u8
```

However, the pressure on SRS is not significant, and the CPU consumption is all on NGINX.

The NGINX edge cluster successfully solved the HLS distribution problem. If you also need to do low-latency live streaming and distribute HTTP-FLV, how to do it? What if you want to support HTTPS HLS or HTTPS-FLV?

NGINX has no problem at all. Now let's see how to work with the SRS Edge Server to implement HTTP-FLV and HLS distribution through NGINX.

## Work with SRS Edge Server

The NGINX edge cluster can also work with the SRS Edge Server to achieve HLS and HTTP-FLV distribution.

```text
+------------+           +------------+
| SRS Origin +--RTMP-->--+ SRS Edge   +
+-----+------+           +----+-------+
      |                       |               +------------+
      |                       +---HTTP-FLV->--+   NGINX    +              +-----------+
      |                                       +   Edge     +--HLS/FLV-->--+ Visitors  +
      +-------HLS--->-------------------------+   Servers  +              +-----------+
                                              +------------+
```

It's very simple to implement. All you need to do is deploy an SRS on the NGINX server and have NGINX work in reverse proxy mode.

```bash
# For SRS streaming, for example:
#   http://r.ossrs.net/live/livestream.flv
location ~ /.+/.*\.(flv)$ {
   proxy_pass http://127.0.0.1:8080$request_uri;
}
```

In this way, HLS is managed by NGINX for caching and back-to-source, while FLV is cached and back-to-source by SRS Edge.

Although this architecture is good, in fact, NGINX can directly serve as an HLS origin server, which can provide even higher performance. Is it possible? No problem at all. Let's see how to use NGINX to distribute HLS completely.

## NGINX Origin Server

Since HLS is just a regular file, it can also be directly used with NGINX as an HLS origin server.

In a super high-concurrency NGINX Edge cluster, a small data center-level cluster can also be formed, with centralized back-to-source from a specific NGINX, which can support even higher concurrency.

Using NGINX to distribute HLS files is actually very simple, you only need to set the root:

```bash
  # For HLS delivery
  location ~ /.+/.*\.(m3u8)$ {
    root /usr/local/srs/objs/nginx/html;
    add_header Cache-Control "public, max-age=10";
  }
  location ~ /.+/.*\.(ts)$ {
    root /usr/local/srs/objs/nginx/html;
    add_header Cache-Control "public, max-age=86400";
  }
```

> Note: Here we set the cache time for m3u8 to 10 seconds, which needs to be adjusted according to the size of the segment.

> Note: Since SRS currently supports HLS variant and implements HLS playback statistics, it is not as efficient as NGINX. See [#2995](https://github.com/ossrs/srs/issues/2995)

> Note: SRS should set `Cache-Control` because the segment service can dynamically set the correct cache time to reduce latency. See [#2991](https://github.com/ossrs/srs/issues/2991)

## Debugging

How to determine if the cache is effective? You can add a field `upstream_cache_status` in the NGINX log and analyze the NGINX log to determine if the cache is effective:

```bash
log_format  main  '$upstream_cache_status $remote_addr - $remote_user [$time_local] "$request" '
                    '$status $body_bytes_sent "$http_referer" '
                    '"$http_user_agent" "$http_x_forwarded_for"';
access_log  /var/log/nginx/access.log main;
```

The first field is the cache status, which can be analyzed using the following command, for example, to only view the cache status of TS files:

```bash
cat /var/log/nginx/access.log | grep '.ts HTTP' \
  | awk '{print $1}' | sort | uniq -c | sort -r
```

You can see which ones are HIT cache, so you don't need to download files from SRS, but directly get files from NGINX.

You can also directly add this field to the response header, so you can see in the browser whether each request has HIT:

```bash
add_header X-Cache-Status $upstream_cache_status;
```

> Note: Regarding the cache effective time, refer to the definition of the field [proxy_cache_valid](https://nginx.org/en/docs/http/ngx_http_proxy_module.html#proxy_cache_valid), in fact, if the source station specifies `Cache-Control`, it will override this configuration.

## aaPanel Configuration

If you are using aaPanel, you can add a new site, and then write the following configuration in the site's configuration:

```bash
    # For Proxy Cache.
    proxy_cache_path  /tmp/nginx-cache levels=1:2 keys_zone=srs_cache:8m max_size=1000m inactive=600m;
    proxy_temp_path /tmp/nginx-cache/tmp; 

    server {
        listen       80;
        server_name your.domain.com;

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
```

> Note: Generally, when adding a new site in aaPanel, it listens to port 80, and the domain server_name is the domain name you fill in yourself. Other configurations are the same as the aaPanel settings. Alternatively, you can also add the above cache and location configurations to the site settings in aaPanel.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/nginx-for-hls)


