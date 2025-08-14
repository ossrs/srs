---
slug: webrtc-over-tcp
title: SRS - Support WebRTC over TCP
authors: []
tags: [tutorial, video, webrtc, tcp]
custom_edit_url: null
---

# Support WebRTC over TCP

> Written by [Winlin](https://github.com/winlinvip), [李鹏](https://github.com/lipeng19811218)

In many networks, UDP is not available for WebRTC, so TCP is very important to make it highly reliable. SRS supports directly TCP transport for WebRTC, not TURN, which introduce a complex network layer and system. It also makes the LoadBalancer possible to forward TCP packets, because TCP is more stable than UDP for LoadBalancer.

<!--truncate-->

## Why Important?

About two years ago, SRS supported WebRTC. During these years, we have gotten lots of feedback. Some are very important and essential:

* UDP is not available, because firewall in company. There is a tool to detect the UDP port, see [#2843](https://github.com/ossrs/srs/issues/2843) for detail.
* There is a set of protocols and ports for media server, for example, RTMP(TCP/1935), HTTP-FLV/HLS(TCP/80/443), WebRTC(UDP/8000), SRT(UDP/10080) and HTTP-API(TCP/1985). All means cost for DevOps. How to deliver media over less ports? About reuse HTTP API and Stream, please read [#2881](https://github.com/ossrs/srs/issues/2881) for detail.
* UDP is permitted but there is loss, which might be caused by [system settings](https://www.jianshu.com/p/6d4a89359352). However TCP always works well, so it's also very good to have a choice to delivery WebRTC over TCP, see [#2852](https://github.com/ossrs/srs/issues/2852) for detail.

If we could deliver all media packets over HTTP like HTTP-FLV or HLS at TCP port 80 or 443, I think life should be easier, because HTTP is always available. The stream flow is like this:

```bash
Publisher --------RTC------> SRS --------RTC--------> Player
            (over TCP/80)           (over TCP/80)
```

> Note: Actually there is a HTTP API request and response for WebRTC, but here we ignore it. You're able to reuse the same TCP port for HTTP API, HTTP Stream and WebRTC over TCP.

Some experts might argue that `TURN` is also ok to solve this problem, it works like this:

```bash
Publisher -----> TURN ----> SRS -----> TURN -----> Player
         (over TURN/3478).      (over TURN/3478)
```

Obviously, TURN is not a good choice, because:

* Extra network server with dependencies. You need to deploy a network server, and update the debugging tools and systems for this network level.
* Extra protocol TURN, even though only few steps, but how to monitor and debug it? Especially need to work with other protocols, such as DTLS or SRTP over TURN.
* Delay and cost for a layer of network server and protocol stack. All these costs turn to CPU and bandwidth consuming.

In short, the best solution for WebRTC over TCP, is directly use TCP for WebRTC, not TURN. Please read detail of bellow RFC:

* SDP and ICE: [TCP Candidates with Interactive Connectivity Establishment (ICE)](https://www.rfc-editor.org/rfc/rfc6544)
* RTP over TCP: [Framing RTP and RTCP Packets over Connection-Oriented Transport](https://www.rfc-editor.org/rfc/rfc4571)

Next is about the status of SRS.

## What's Now?

SRS 5.0 has supported WebRTC over TCP:

* All HTTP API, HTTP Stream and WebRTC over TCP reuses one TCP port, such as TCP(443) for HTTPS.
* Support directly transport over UDP or TCP, no dependency of TURN, no extra system and resource cost.
* Works very well with [Proxy(Not Implemented)](https://github.com/ossrs/srs/issues/3138) and [Cluster(Not Implemented)](https://github.com/ossrs/srs/issues/2091), for load balancing and system capacity.

> Note: Please upgrade to `v5.0.60+`, please remember to check for both building or docker image.

First, we use TCP(8080) for WebRTC, which reuse port with HTTP Server:

```bash
docker run --rm -it -p 8080:8080/tcp \
  -e CANDIDATE="192.168.3.82" \
  -e SRS_HTTP_API_LISTEN=8080 \
  -e SRS_RTC_SERVER_TCP_ENABLED=on \
  -e SRS_RTC_SERVER_TCP_LISTEN=8080 \
  -e SRS_RTC_SERVER_PROTOCOL=tcp \
  registry.cn-hangzhou.aliyuncs.com/ossrs/srs:v5.0.60
```

* Publish WebRTC over TCP: [http://localhost:8080/rtc/v1/whip/?app=live&stream=livestream](http://localhost:8080/players/whip.html?autostart=true&api=8080)
* Play WebRTC over TCP: [http://localhost:8080/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true&api=8080)

SRS allows coverting RTC to live streaming, so we use TCP(8080) for both WebRTC and HTTP live streaming:

```bash
docker run --rm -it -p 8080:8080/tcp \
  -e CANDIDATE="192.168.3.82" \
  -e SRS_VHOST_RTC_RTC_TO_RTMP=on \
  -e SRS_HTTP_API_LISTEN=8080 \
  -e SRS_RTC_SERVER_TCP_ENABLED=on \
  -e SRS_RTC_SERVER_TCP_LISTEN=8080 \
  -e SRS_RTC_SERVER_PROTOCOL=tcp \
  registry.cn-hangzhou.aliyuncs.com/ossrs/srs:v5.0.60
```

* Publish WebRTC over TCP: [http://localhost:8080/rtc/v1/whip/?app=live&stream=livestream](http://localhost:8080/players/whip.html?autostart=true&api=8080)
* Play WebRTC over TCP: [http://localhost:8080/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true&api=8080)
* Play HTTP FLV: [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true)
* Play HLS: [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?stream=livestream.m3u8&autostart=true)

If not publishing by `localhost`, you must use HTTPS, so we use TCP(8088) for both WebRTC and HTTPS live streaming:

```bash
docker run --rm -it -p 8088:8088/tcp \
  -e CANDIDATE="192.168.3.82" \
  -e SRS_VHOST_RTC_RTC_TO_RTMP=on \
  -e SRS_HTTP_API_LISTEN=8080 \
  -e SRS_HTTP_API_HTTPS_ENABLED=on \
  -e SRS_HTTP_API_HTTPS_LISTEN=8088 \
  -e SRS_HTTP_SERVER_HTTTPS_ENABLED=on \
  -e SRS_RTC_SERVER_TCP_ENABLED=on \
  -e SRS_RTC_SERVER_TCP_LISTEN=8088 \
  -e SRS_RTC_SERVER_PROTOCOL=tcp \
  registry.cn-hangzhou.aliyuncs.com/ossrs/srs:v5.0.60
```

* Publish WebRTC over TCP: [webrtc://localhost:8088/live/livestream](https://localhost:8088/players/rtc_publisher.html?api=8088&autostart=true)
* Play WebRTC over TCP: [webrtc://localhost:8088/live/livestream](https://localhost:8088/players/rtc_player.html?api=8088&autostart=true)
* Play HTTP FLV: [https://localhost:8088/live/livestream.flv](https://localhost:8088/players/srs_player.html?schema=https&port=8088&autostart=true)
* Play HLS: [https://localhost:8088/live/livestream.m3u8](https://localhost:8088/players/srs_player.html?schema=https&port=8088&stream=livestream.m3u8&autostart=true)

> Note: You can also use IP to access the webpage. Note that because of self-signed cert files for HTTPS, you must click the blank of page and type string `thisisunsafe`

We configure SRS by environment variables, but you're also able to use configuration file:

```bash
rtc_server {
    # For WebRTC over TCP directly, not TURN, see https://github.com/ossrs/srs/issues/2852
    # Some network does not support UDP, or not very well, so we use TCP like HTTP/80 port for firewall traversing.
    tcp {
        # Whether enable WebRTC over TCP.
        # Overwrite by env SRS_RTC_SERVER_TCP_ENABLED
        # Default: off
        enabled off;
        # The TCP listen port for WebRTC. Highly recommend is some normally used ports, such as TCP/80, TCP/443,
        # TCP/8000, TCP/8080 etc. However SRS default to TCP/8000 corresponding to UDP/8000.
        # Overwrite by env SRS_RTC_SERVER_TCP_LISTEN
        # Default: 8000
        listen 8000;
    }
    # The protocol for candidate to use, it can be:
    #       udp         Generate UDP candidates. Note that UDP server is always enabled for WebRTC.
    #       tcp         Generate TCP candidates. Fail if rtc_server.tcp(WebRTC over TCP) is disabled.
    #       all         Generate UDP+TCP candidates. Ignore if rtc_server.tcp(WebRTC over TCP) is disabled.
    # Note that if both are connected, we will use the first connected(DTLS done) one.
    # Overwrite by env SRS_RTC_SERVER_PROTOCOL
    # Default: udp
    protocol udp;
}
```

We demostrate the reusing port of WebRTC and HTTP Stream, but you're able to use dedicated TCP ports for HTTP and WebRTC over TCP.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one, 
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from 
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms, 
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Future Plan

We're developing SRS 5.0, and we might close features at the end of 2022.

## Contact

Welcome to join the SRS community by [discord](https://discord.gg/yZ4BnPmHAd).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2022-09-05-WebRTC-Over-TCP)

