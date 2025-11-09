---
title: WebRTC
sidebar_label: WebRTC
hide_title: false
hide_table_of_contents: false
---

# WebRTC

WebRTC is an online real-time communication solution open-sourced by Google. In simple terms, it is an 
internet audio and video conference system. As it follows the RFC standard protocol and is supported 
by browsers, its boundaries are constantly expanding. It is used in low-latency audio and video scenarios,
such as online meetings, live streaming video chat with guests, low-latency live broadcasts, remote 
robot control, remote desktop, cloud video game, smart doorbells, and live web page streaming.

WebRTC is essentially a standard for direct communication between two web browsers, mainly consisting
of signaling and media protocols. Signaling deals with the negotiation of capabilities between two
devices, such as supported encoding and decoding abilities. Media handles the encryption and low-latency
transmission of media packets between devices. In addition, WebRTC itself also implements audio processing
technologies like 3A, network congestion control such as NACK, FEC, and GCC, audio and video encoding
and decoding, as well as smooth and low-latency playback technologies.

```bash
+----------------+                        +----------------+
+    Browser     +----<--Signaling----->--+    Browser     +
+ (like Chrome)  +----<----Media----->----+ (like Chrome)  +
+----------------+                        +----------------+
```

> Note: WebRTC is now an official RFC standard, so it is supported by various browsers. There are many
> open-source implementations, making it available not only in browsers but also in mobile browsers and
> native libraries. For simplicity, in this post, the term "browser" refers to any client or device that
> supports the WebRTC protocol.

In reality, on the internet, it's almost impossible for two browsers to communicate directly, especially 
when they're not on the same local network and are located far apart, like in different cities or countries. 
The data transfer between the two browsers goes through many network routers and firewalls, making it hard 
to ensure good transmission quality. Therefore, in practical applications, data needs to be relayed through 
servers. There are several types of WebRTC servers to help with this process:

* Signaling Server: This is a service that helps two browsers exchange SDP (Session Description Protocol) information. For multi-person conferences, room services are needed, but the main purpose is still to exchange SDP between browsers. In the streaming media field, to enable WebRTC for streaming and playback, similar to RTMP/SRT/HLS streaming, the WHIP/WHEP protocols have been designed.
* TURN Server: Relay service that helps two browsers forward media data between them. This is a transparent forwarding service without data caching, so during multi-person meetings, browsers need to transfer `N*N + N*(N-2)` copies of data. It is generally used in very few communication scenarios, such as one-on-one.
* SFU Server: Selective forwarding service with cached data on the server, allowing browsers to upload only one copy of data, which the server then replicates to other participants. SRS is an example of an SFU. For more information on SFU's role, refer to [this link](https://stackoverflow.com/a/75491178/17679565). Most current WebRTC servers are SFU servers, with `N*N` streams being transferred, reducing the amount of data transfer by `N*(N-2)` compared to TURN servers. This helps solve most transmission issues.
* MCU Server: Multipoint Control Unit Server, the server merges the streams in a conference into one, so the browser only needs to transfer `N*2` sets of data, uploading one and downloading one. However, due to the need for encoding and decoding, the number of streams supported by the server is an order of magnitude less than SFU, and it is only used in certain specific scenarios. For more details, refer to [#3625](https://github.com/ossrs/srs/discussions/3625).

We primarily focus on explaining the SFU (Selective Forwarding Unit) workflow, as it is widely used in
WebRTC servers, and it essentially functions like a browser:

```bash
+----------------+                        +---------+
+    Browser     +----<--Signaling----->--+   SFU   +
+ (like Chrome)  +----<----Media----->----+  Server +
+----------------+                        +---------+
```

> Note: Generally, SFUs have Signaling capabilities. In fact, RTMP addresses can be considered as a very
> simplified signaling protocol. However, WebRTC signaling requires more complex negotiation of media and
> transport capabilities. In complex WebRTC systems, there might be separate Signaling and Room clusters,
> but SFUs also have simplified Signaling capabilities, which may be used for communication with other
> services.

SRS is a media server that provides Signaling and SFU Server capabilities. Unlike other SFUs like Janus, 
SRS is based on streams. Even though there can be multiple participants in a room, essentially, someone is 
pushing a stream, and others are subscribing to it. This way, it avoids coupling all the streams in a room to 
a single SFU transmission and can distribute them across multiple SFU transmissions, allowing for larger 
conferences with more participants.

SRS supports signaling protocols WHIP and WHEP. For more details, please refer to the [HTTP API](#http-api) 
section. Unlike live streaming, signaling and media are separated, so you need to set up Candidates, see 
[Candidate](#config-candidate). Media uses UDP by default, but if UDP is unavailable, you can use TCP as described in 
[TCP](#webrtc-over-tcp). If you encounter issues, it could be due to incorrect Candidate settings or firewall/port 
restrictions, refer to [Connectivity](#connection-failures) and use the provided tools to check. SRS also supports 
converting between different protocols, such as streaming RTMP and viewing with WebRTC, as explained in 
[RTMP to WebRTC](#rtmp-to-rtc), or streaming with WebRTC and viewing with HLS, as described in 
[RTC to RTMP](#rtc-to-rtmp).

SRS supported the WebRTC protocol in 2020. For more information on the development process, please refer
to [#307](https://github.com/ossrs/srs/issues/307).

## Config

There are some config for WebRTC, please see `full.conf` for more:

```bash
rtc_server {
    # Whether enable WebRTC server.
    # Overwrite by env SRS_RTC_SERVER_ENABLED
    # default: off
    enabled on;
    # The udp listen port, we will reuse it for connections.
    # Overwrite by env SRS_RTC_SERVER_LISTEN
    # default: 8000
    listen 8000;
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
    # The exposed candidate IPs, response in SDP candidate line. It can be:
    #       *           Retrieve server IP automatically, from all network interfaces.
    #       $CANDIDATE  Read the IP from ENV variable, use * if not set.
    #       x.x.x.x     A specified IP address or DNS name, use * if 0.0.0.0.
    # @remark For Firefox, the candidate MUST be IP, MUST NOT be DNS name, see https://bugzilla.mozilla.org/show_bug.cgi?id=1239006
    # @see https://ossrs.net/lts/zh-cn/docs/v4/doc/webrtc#config-candidate
    # Overwrite by env SRS_RTC_SERVER_CANDIDATE
    # default: *
    candidate *;
}

vhost rtc.vhost.srs.com {
    rtc {
        # Whether enable WebRTC server.
        # Overwrite by env SRS_VHOST_RTC_ENABLED for all vhosts.
        # default: off
        enabled on;
        # Whether support NACK.
        # default: on
        nack on;
        # Whether support TWCC.
        # default: on
        twcc on;
        # Whether enable transmuxing RTMP to RTC.
        # If enabled, transcode aac to opus.
        # Overwrite by env SRS_VHOST_RTC_RTMP_TO_RTC for all vhosts.
        # default: off
        rtmp_to_rtc off;
        # Whether enable transmuxing RTC to RTMP.
        # Overwrite by env SRS_VHOST_RTC_RTC_TO_RTMP for all vhosts.
        # Default: off
        rtc_to_rtmp off;
    }
}
```

The config `rtc_server` is global configuration for RTC, for example:
* `enabled`：Whether enable WebRTC server.
* `listen`：The udp listen port, we will reuse it for connections.
* `candidate`：The exposed candidate IPs, response in SDP candidate line. Please read [Config: Candidate](./webrtc.md#config-candidate) for detail.
* `tcp.listen`: Whether enable WebRTC over TCP. Please read [WebRTC over TCP](./webrtc.md#webrtc-over-tcp) for detail.

For each vhost, the configuration is `rtc` section, for example:
* `rtc.enabled`：Whether enable WebRTC server for this vhost.
* `rtc.rtmp_to_rtc`：Whether enable transmuxing RTMP to RTC.
* `rtc.rtc_to_rtmp`：Whether enable transmuxing RTC to RTMP.
* `rtc.stun_timeout`：The timeout in seconds for session timeout.
* `rtc.nack`：Whether support NACK for ARQ.
* `rtc.twcc`：Whether support TWCC for congestion feedback.
* `rtc.dtls_role`：The role of dtls when peer is actpass: passive or active.

## Config: Candidate

Please note that `candidate` is essential important, and most failure is caused by wrong `candidate`, so be careful.

The easiest method to modify the `candidate` involves indicating the `eip` in the URL. For instance, if your server 
is `192.168.3.10`, utilize this URL:

* [http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream&eip=192.168.3.10](http://localhost:8080/players/whip.html?eip=192.168.3.10)

Moreover, the easiest and most direct method to modify the default UDP port `8000`, particularly when it is 
behind a load balancer or proxy, involves utilizing the `eip`. For example, if you employ UDP `18000` as the port, 
consider using this URL:

* [http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream&eip=192.168.3.10:18000](http://localhost:8080/players/whip.html?eip=192.168.3.10:18000)

As it shows, `candidate` is server IP to connect to, SRS will response it in SDP answer as `candidate`, like this one:

```bash
type: answer, sdp: v=0
a=candidate:0 1 udp 2130706431 192.168.3.6 8000 typ host generation 0
```

So the `192.168.3.6 8000` is an endpoint that client could access. There be some IP you can use:
* Config as fixed IP, such as `candidate 192.168.3.6;`
* Use `ifconfig` to get server IP and pass by environment variable, such as `candidate $CANDIDATE;`
* Detect automatically, first by environment, then use server network interface IP, such as `candidate *;`, we will explain at bellow.
* Specify the `?eip=x` in URL, such as: `webrtc://192.168.3.6/live/livestream?eip=192.168.3.6`
* Normally API is provided by SRS, so you're able to use hostname of HTTP-API as `candidate`, we will explain at bellow.

Configurations for automatically detect the IP for `candidate`:
* `candidate *;` or `candidate 0.0.0.0;` means detect the network interface IP.
* `use_auto_detect_network_ip on;` If disabled, never detect the IP automatically.
* `ip_family ipv4;` To filter the IP if automatically detect.

Configurations for using HTTP-API hostname as `candidate`:
* `api_as_candidates on;` If disabled, never use HTTP API hostname as candidate.
* `resolve_api_domain on;` If hostname is domain name, resolve to IP address. Note that Firefox does not support domain name.
* `keep_api_domain on;` Whether keep the domain name to resolve it by client.

> Note: Please note that if no `candidate` specified, SRS will use one automatically detected IP.

In short, the `candidate` must be a IP address that client could connect to.

Use command `ifconfig` to retrieve the IP:

```bash
# For macOS
CANDIDATE=$(ifconfig en0 inet| grep 'inet '|awk '{print $2}')

# For CentOS
CANDIDATE=$(ifconfig eth0|grep 'inet '|awk '{print $2}')

# Directly set ip.
CANDIDATE="192.168.3.10"
```

Pass it to SRS by ENV:

```bash
env CANDIDATE="192.168.3.10" \
  ./objs/srs -c conf/rtc.conf
```

For example, to run SRS in docker, and setup the CANDIDATE:

```bash
export CANDIDATE="192.168.3.10"
docker run --rm --env CANDIDATE=$CANDIDATE \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
  ossrs/srs:5 \
  objs/srs -c conf/rtc.conf
```

> Note：About the usage of srs-docker, please read [srs-docker](https://github.com/ossrs/dev-docker/tree/v4#usage).

## Stream URL

In SRS, both live streaming and WebRTC are based on the concept of `streams`. So, the URL definition for 
streams is very consistent. Here are some different stream addresses for various protocols in SRS, which 
you can access after installing SRS:

* Publish or play stream over RTMP: `rtmp://localhost/live/livestream`
* Play stream over HTTP-FLV: [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html)
* Play stream over HLS: [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?stream=livestream.m3u8)
* Publish stream over WHIP: [http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream](http://localhost:8080/players/whip.html)
* Play stream over WHEP: [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html)

> Remark: Since Flash is disabled, RTMP streams cannot be played in Chrome. Please use VLC to play them.

Before WHIP and WHEP were introduced, SRS supported another format with a different HTTP API format, but it 
still exchanged SDP. It is no longer recommended:

* Publish: [webrtc://localhost/live/livestream](http://localhost:8080/players/rtc_publisher.html)
* Play: [webrtc://localhost/live/livestream](http://localhost:8080/players/rtc_player.html)

> Note: SRT addresses are not provided here because their design is not in a common URL format.

## WebRTC over TCP

In many networks, UDP is not available for WebRTC, so TCP is very important to make it highly reliable. SRS supports directly TCP transport for WebRTC, not TURN, which introduce a complex network layer and system. It also makes the LoadBalancer possible to forward TCP packets, because TCP is more stable than UDP for LoadBalancer.

* All HTTP API, HTTP Stream and WebRTC over TCP reuses one TCP port, such as TCP(443) for HTTPS.
* Support directly transport over UDP or TCP, no dependency of TURN, no extra system and resource cost.
* Works very well with [Proxy(Not Implemented)](https://github.com/ossrs/srs/issues/3138) and [Cluster(Not Implemented)](https://github.com/ossrs/srs/issues/2091), for load balancing and system capacity.

Run SRS with WebRTC over TCP, by default the port is 8000:

```bash
docker run --rm -it -p 8080:8080 -p 1985:1985 -p 8000:8000 \
  -e CANDIDATE="192.168.3.82" \
  -e SRS_RTC_SERVER_TCP_ENABLED=on \
  -e SRS_RTC_SERVER_PROTOCOL=tcp \
  -e SRS_RTC_SERVER_TCP_LISTEN=8000 \
  ossrs/srs:v5
```

Please use [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) to publish stream:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

* Play WebRTC over TCP: [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true)
* Play HTTP FLV: [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true)
* Play HLS: [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?stream=livestream.m3u8&autostart=true)

> Note: We config SRS by environment variables, you're able to use config file also.

> Note: We use dedicated TCP port, for example, HTTP API(1985), HTTP Stream(8080) and WebRTC over TCP(8000), you're able to reuse one TCP port at HTTP Stream(8080).

## HTTP API

SRS supports WHIP and WHEP protocols. After installing SRS, you can test it with the following links:

* To use WHIP for streaming: [http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream](http://localhost:8080/players/whip.html)
* To use WHEP for playback: [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html)

For details on the protocols, refer to [WHIP](./http-api.md#webrtc-publish) and [WHEP](./http-api.md#webrtc-play).
Bellow is the workflow:

[![](/img/doc-whip-whep-workflow.png)](https://www.figma.com/file/fA75Nl6Fr6v8hsrJba5Xrn/How-Does-WHIP%2FWHEP-Work%3F?type=whiteboard&node-id=0-1)

If you install SRS on a Mac or Linux, you can test the local SRS service with localhost. However, if you're using 
Windows, a remote Linux server, or need to test on other devices, you must use HTTPS for WHIP streaming, while 
WHEP can still use HTTP. To enable SRS HTTPS, refer to [HTTPS API](./http-api.md#https-api), or use a web server 
proxy like Nginx by referring to [HTTPS Proxy](./http-api.md#http-and-https-proxy).

If you need to test if the HTTP API is working properly, you can use the `curl` tool. For more details, please
refer to [Connectivity Check](#connection-failures).

## Connection Failures

Some developer come to SRS community to get help, because they get error when use OBS WHIP to connect to online WHIP
server, because online server must use HTTPS and the UDP port might be more available, and it's hard to debug or
login to the online server for privacy or network issue.

So we find some ways to troubleshoot the connection failures in OBS WHIP, generally it's caused by HTTPS API setup
or UDP port not available issue.

Use curl to test WHIP HTTP or HTTPS API:

```bash
curl "http://localhost:1985/rtc/v1/whip/?ice-ufrag=6pk11386&ice-pwd=l91z529147ri9163933p51c4&app=live&stream=livestream-$(date +%s)" \
  -H 'Origin: http://localhost' -H 'Referer: http://localhost' \
  -H 'Accept: */*' -H 'Content-type: application/sdp' \
  -H 'User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)' \
  --data-raw $'v=0\r\na=group:BUNDLE 0 1\r\nm=audio 9 UDP/TLS/RTP/SAVPF 111\r\nc=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:J8X7\r\na=ice-pwd:Dpq7/fW/osYcPeLsCW2Ek1JH\r\na=setup:actpass\r\na=mid:0\r\na=sendonly\r\na=msid:- audio\r\na=rtcp-mux\r\na=rtpmap:111 opus/48000/2\r\na=ssrc:3184534672 cname:stream\r\nm=video 9 UDP/TLS/RTP/SAVPF 106\r\nc=IN IP4 0.0.0.0\r\na=rtcp:9 IN IP4 0.0.0.0\r\na=ice-ufrag:J8X7\r\na=ice-pwd:Dpq7/fW/osYcPeLsCW2Ek1JH\r\na=setup:actpass\r\na=mid:1\r\na=sendonly\r\na=msid:- video\r\na=rtcp-mux\r\na=rtpmap:106 H264/90000\r\na=ssrc:512761356 cname:stream' \
  -v -k
```

> Note: You can replace `http://localhost` with `https://yourdomain.com` to test HTTPS API.

> Note: For Oryx, you should specify the secret, so please change the `/rtc/v1/whip?ice-ufrag=` to `/rtc/v1/whip?secret=xxx&ice-ufrag=` as such.

> Note: You can also use `eip=ip` or `eip=ip:port` to force SRS to use it as the candidate. Please see [CANDIDATE](#config-candidate) for details.

The answer contains the candidate, the UDP server IP, such as `127.0.0.1`:

```
a=candidate:0 1 udp 2130706431 127.0.0.1 8000 typ host generation 0
```

Use `nc` to send UDP packet to SRS WHIP server:

```bash
echo -en "\x00\x01\x00\x50\x21\x12\xa4\x42\x74\x79\x6d\x7a\x41\x51\x2b\x2f\x4a\x4b\x77\x52\x00\x06\x00\x0d\x36\x70\x6b\x31\x31\x33\x38\x36\x3a\x4a\x38\x58\x37\x00\x00\x00\xc0\x57\x00\x04\x00\x01\x00\x0a\x80\x2a\x00\x08\xda\xad\x1d\xce\xe8\x95\x5a\x83\x00\x24\x00\x04\x6e\x7f\x1e\xff\x00\x08\x00\x14\x56\x8f\x1e\x1e\x4f\x5f\x17\xf9\x2e\xa1\xec\xbd\x51\xd9\xa2\x27\xe4\xfd\xda\xb1\x80\x28\x00\x04\x84\xd3\x5a\x79" \
  |nc -w 3 -u 127.0.0.1 8000 |od -Ax -c -t x1 |grep '000' && \
  echo "Success" || echo "Failed"
```

> Note: You also can use `nc` or [server.go](https://github.com/ossrs/srs/pull/3837) as the UDP server for test.

If use SRS as WHIP server, should response with:

```
0000000  001 001  \0   @   ! 022 244   B   t   y   m   z   A   Q   +   /
0000010    J   K   w   R  \0 006  \0  \r   6   p   k   1   1   3   8   6
0000020    :   J   8   X   7  \0  \0  \0  \0      \0  \b  \0 001 376   `
0000030    ầ  **  ** 027  \0  \b  \0 024 206 263   +   ŉ  ** 025   G 215
0000040    I 335   P   ^   "   7   }   N   ? 017 037 224 200   (  \0 004
0000050  303   < 250 272                                                
0000054
Success
```

> Note: Should be SRS 5.0.191+, see [#3837](https://github.com/ossrs/srs/pull/3837), you can also use
> [server.go](https://github.com/ossrs/srs/issues/2843) as the UDP server for test.

## RTMP to RTC

Please use `conf/rtmp2rtc.conf` as config.

```bash
export CANDIDATE="192.168.1.10"
docker run --rm --env CANDIDATE=$CANDIDATE \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
  ossrs/srs:5 \
  objs/srs -c conf/rtmp2rtc.conf
```

> Note: Please set CANDIDATE as the ip of server, please read [CANDIDATE](./webrtc.md#config-candidate).

Use FFmpeg docker to push to localhost:

```bash
docker run --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -c copy -f flv rtmp://host.docker.internal/live/livestream
```

Play the stream in browser:

* WebRTC：[http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html?autostart=true)
* HTTP-FLV：[http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)

## RTC to RTC

Please use `conf/rtc.conf` as config.

```bash
export CANDIDATE="192.168.1.10"
docker run --rm --env CANDIDATE=$CANDIDATE \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
  ossrs/srs:5 \
  objs/srs -c conf/rtc.conf
```

> Note: Please set CANDIDATE as the ip of server, please read [CANDIDATE](./webrtc.md#config-candidate).

Play the stream in browser:

* Publish stream over WHIP: [http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream](http://localhost:8080/players/whip.html)
* Play stream over WHEP: [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html)

> Remark: Note that if not localhost, the WebRTC publisher should be HTTPS page.

## RTC to RTMP

Please use `conf/rtc2rtmp.conf` as config.

```bash
export CANDIDATE="192.168.1.10"
docker run --rm --env CANDIDATE=$CANDIDATE \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
  ossrs/srs:5 \
  objs/srs -c conf/rtc2rtmp.conf
```

> Note: Please set CANDIDATE as the ip of server, please read [CANDIDATE](./webrtc.md#config-candidate).

The streams:

* Publish stream over WHIP: [http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream](http://localhost:8080/players/whip.html)
* Play stream over WHEP: [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream](http://localhost:8080/players/whep.html)
* HTTP-FLV：[http://localhost:8080/live/show.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=show.flv)
* RTMP by VLC：rtmp://localhost/live/show

## AV1 Codec Support

SRS supports AV1 codec for WebRTC-to-WebRTC streaming since v4.0.91 ([#2324](https://github.com/ossrs/srs/pull/2324)).
AV1 is a royalty-free codec that saves 30-50% bandwidth compared to H.264. SRS implements AV1 as relay-only (SFU mode),
accepting AV1 streams via WHIP and forwarding to WHEP players without transcoding. AV1 streams cannot be converted to
RTMP/HLS or recorded to DVR.

To use AV1, add the `codec=av1` query parameter to WHIP/WHEP URLs:

* Publish: `http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream&codec=av1`
* Play: `http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream&codec=av1`

Browser support: Chrome/Edge M90+, Firefox (full support), Safari (decode only). Use H.264 if you need RTMP/HLS conversion,
DVR recording, or maximum compatibility.

## VP9 Codec Support

SRS supports VP9 codec for WebRTC-to-WebRTC streaming since v7.0.123 ([#4548](https://github.com/ossrs/srs/issues/4548)).
VP9 is a royalty-free codec that saves 20-40% bandwidth compared to H.264. VP9 works better than H.264/H.265 with congestion control
in WebRTC, making it ideal for keeping streams live under network fluctuations. SRS implements VP9 as relay-only (SFU mode),
accepting VP9 streams via WHIP and forwarding to WHEP players without transcoding. VP9 streams cannot be converted to
RTMP/HLS or recorded to DVR.

To use VP9, add the `codec=vp9` query parameter to WHIP/WHEP URLs:

* Publish: `http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream&codec=vp9`
* Play: `http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream&codec=vp9`

Browser support: Chrome/Edge M29+, Firefox M28+, Opera M16+. Safari does not support VP9. Use H.264 if you need RTMP/HLS conversion,
DVR recording, or Safari compatibility.

## SFU: One to One

Please use `conf/rtc.conf` as config.

```bash
export CANDIDATE="192.168.1.10"
docker run --rm --env CANDIDATE=$CANDIDATE \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
  ossrs/srs:5 \
  objs/srs -c conf/rtc.conf
```

> Note: Please set CANDIDATE as the ip of server, please read [CANDIDATE](./webrtc.md#config-candidate).

Then startup the signaling, please read [usage](https://github.com/ossrs/signaling#usage):

```bash
docker run --rm -p 1989:1989 ossrs/signaling:1
```

Use HTTPS proxy [httpx-static](https://github.com/ossrs/go-oryx/tree/develop/httpx-static#usage) as api gateway:

```bash
export CANDIDATE="192.168.1.10"
docker run --rm -p 80:80 -p 443:443 ossrs/httpx:1 \
    ./bin/httpx-static -http 80 -https 443 -ssk ./etc/server.key -ssc ./etc/server.crt \
          -proxy http://$CANDIDATE:1989/sig -proxy http://$CANDIDATE:1985/rtc \
          -proxy http://$CANDIDATE:8080/
```

To open [http://localhost/demos/one2one.html?autostart=true](http://localhost/demos/one2one.html?autostart=true)

Or by the IP [https://192.168.3.6/demos/one2one.html?autostart=true](https://192.168.3.6/demos/one2one.html?autostart=true)

> Note: For self-sign certificate, please type `thisisunsafe` to accept it.

## SFU: Video Room

Please follow [SFU: One to One](./webrtc.md#sfu-one-to-one), and open the bellow demo pages.

To open [http://localhost/demos/room.html?autostart=true](http://localhost/demos/room.html?autostart=true)

Or by the IP [https://192.168.3.6/demos/room.html?autostart=true](https://192.168.3.6/demos/room.html?autostart=true)

> Note: For self-sign certificate, please type `thisisunsafe` to accept it.

## Room to Live

Please follow [SFU: One to One](./webrtc.md#sfu-one-to-one), and please convert RTC to RTMP, for FFmpeg to mix the streams.

```bash
export CANDIDATE="192.168.1.10"
docker run --rm --env CANDIDATE=$CANDIDATE \
  -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
  ossrs/srs:5 \
  objs/srs -c conf/rtc2rtmp.conf
```

If use FFmpeg to mix streams, there is a FFmpeg CLI on the demo page, for example:

```bash
ffmpeg -f flv -i rtmp://192.168.3.6/live/alice -f flv -i rtmp://192.168.3.6/live/314d0336 \
     -filter_complex "[1:v]scale=w=96:h=72[ckout];[0:v][ckout]overlay=x=W-w-10:y=H-h-10[out]" -map "[out]" \
     -c:v libx264 -profile:v high -preset medium \
     -filter_complex amix -c:a aac \
     -f flv rtmp://192.168.3.6/live/merge
```

Input:
* rtmp://192.168.3.6/live/alice
* rtmp://192.168.3.6/live/314d0336

Output:
* rtmp://192.168.3.6/live/merge

Winlin 2020.02

## IPv6

SRS (v7.0.67+) supports IPv6 for WebRTC protocols, enabling dual-stack (IPv4/IPv6) operation for both UDP and TCP media transport. This includes support for WHIP/WHEP signaling and media transmission over IPv6.

IPv6 support is enabled automatically when SRS detects IPv6 addresses in the configuration. Configure the RTC server to listen on IPv6 addresses:

```bash
rtc_server {
    enabled on;
    # Listen on both IPv4 and IPv6 for UDP media
    listen 8000 [::]:8000;

    # For WebRTC over TCP
    tcp {
        enabled on;
        listen 8000 [::]:8000;
    }
}

# HTTP API server for WHIP/WHEP over IPv6
http_api {
    enabled on;
    listen 1985 [::]:1985;
}

# HTTPS API server for secure WHIP/WHEP over IPv6
https_api {
    enabled on;
    listen 1990 [::]:1990;
    key ./conf/server.key;
    cert ./conf/server.crt;
}
```

Publish using WHIP via IPv6:
- WHIP URL: `http://[::1]:1985/rtc/v1/whip/?app=live&stream=livestream`
- Test page: [http://[::1]:8080/players/whip.html](http://[::1]:8080/players/whip.html)

Play using WHEP via IPv6:
- WHEP URL: `http://[::1]:1985/rtc/v1/whep/?app=live&stream=livestream`
- Test page: [http://[::1]:8080/players/whep.html](http://[::1]:8080/players/whep.html)

For secure connections over IPv6:

Publish using WHIP via HTTPS IPv6:
- WHIP URL: `https://[::1]:1990/rtc/v1/whip/?app=live&stream=livestream`
- Test page: [https://[::1]:8088/players/whip.html](https://[::1]:8088/players/whip.html)

Play using WHEP via HTTPS IPv6:
- WHEP URL: `https://[::1]:1990/rtc/v1/whep/?app=live&stream=livestream`
- Test page: [https://[::1]:8088/players/whep.html](https://[::1]:8088/players/whep.html)

SRS supports dual-stack WebRTC, allowing both IPv4 and IPv6 clients to connect simultaneously:

```bash
rtc_server {
    enabled on;
    # Listen on both IPv4 and IPv6
    listen 8000 [::]:8000;

    # Configure candidates for both protocols
    candidate 192.168.1.100 [2001:db8::1];
}
```

This enables:
- IPv4 clients to connect via: `http://192.168.1.100:1985/rtc/v1/whip/`
- IPv6 clients to connect via: `http://[2001:db8::1]:1985/rtc/v1/whip/`

## Known Limitation: Initial Audio Loss

When publishing WebRTC streams, you may notice that the **first 4-6 seconds of audio are missing** in recordings (DVR),
RTMP playback, or HTTP-FLV streams. This is a **known limitation** of WebRTC's audio/video synchronization mechanism,
not a bug.

**Root Cause**: WebRTC uses RTCP Sender Reports (SR) to synchronize audio and video timestamps. When a WebRTC stream
starts, both audio and video RTP packets arrive immediately. However, SRS needs RTCP Sender Reports to calculate proper
timestamps for synchronizing audio and video. The A/V sync calculation requires **TWO** RTCP Sender Reports to establish
the timing rate between RTP timestamps and system time. All RTP packets (both audio and video) with `avsync_time <= 0`
are **discarded** to avoid timestamp problems in the live source. RTCP Sender Reports typically arrive every 2-3 seconds.
After the **second** SR arrives (~4-6 seconds), the A/V sync rate is calculated, and packets start being accepted. If DVR
is configured with `dvr_wait_keyframe on`, recording starts at the first video keyframe anyway. Video keyframes typically
arrive every 2-4 seconds, so by the time the first keyframe arrives, A/V sync is often already established. However, audio
packets that arrived before sync was established are **permanently lost**.

**Why This Won't Be Fixed**: This is a **fundamental limitation** of WebRTC's A/V synchronization mechanism. The RTCP-based
A/V synchronization is essential for WebRTC. Without it, audio and video timestamps would be misaligned, causing severe sync
issues throughout the entire stream. The current design prioritizes **correct A/V synchronization** over capturing the first
few seconds. This is a reasonable trade-off for most live streaming scenarios where streams run for extended periods (minutes
to hours), losing 4-6 seconds at the start is acceptable, and perfect A/V sync throughout the stream is critical. Fixing this
would require fundamentally redesigning the WebRTC A/V sync mechanism, which is extremely complex and risky.

**Related Issues**: [#4418](https://github.com/ossrs/srs/issues/4418), [#4151](https://github.com/ossrs/srs/issues/4151),
[#4076](https://github.com/ossrs/srs/issues/4076)

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/webrtc)


