---
title: RTMP URL
sidebar_label: RTMP URL
hide_title: false
hide_table_of_contents: false
---

# RTMP URL/Vhost

The url of RTMP is simple, and the vhost is not a new concept, although it's easy to confuse for the fresh.
What is vhost? What is app? Why FMLE should input the app and stream?

Vhost(Virtual Host) is used to seperate customers or businesses.
Let's take a look at the typical use scenaio of vhost.

![](/img/doc-main-concepts-rtmp-url-vhost-001.png)

The benifit of RTMP and HLS, see: [HLS](./hls.md)

## Use Scenario

The use scenario of vhost:
* Multiple customers cloud: For example, CDN(content delivery network) serves multiple customers. How does CDN to seperate customer and stat the data? Maybe stream path is duplicated, for example, live/livestream is the most frequently used app and stream. The vhost, similar to the virtual server, provides abstract for multiple customers.
* Different config: For example, FMLE publish h264+mp3, which should be transcoded to h264+aac. We can use vhost to use different config, h264+mp3 disabled hls while the h264+aac vhost enalbe hls.

In a word, vhost is the element of config, to seperate customer and apply different config.

## Standard RTMP URL

Standard RTMP URL is the most compatible URL, for all servers and players can identify. The RTMP URL is similar to the HTTP URL:

| HTTP | Schema | Host | Port | App | Stream |
| ----- | ----- | ----- | ----| ---- | ---- |
| http://192.168.1.10:80/players/srs_player.html | http | 192.168.1.10 | 80 | players | srs_player.html|
| rtmp://192.168.1.10:1935/live/livestream | rtmp | 192.168.1.10 | 1935 | live | livestream |

It is:
* Schema：The protocol prefix, HTTP/HTTPS for HTTP protocol, and RTMP/RTMPS/RTMPE/RTMPT for RTMP protocol, while the RTMFP is adobe flash p2p protocol.
* Host：The server ip or dns name to connect to. It is dns name for CDN, and the dns name is used as the vhost for the specified customer.
* Port：The tcp port, default 80 for HTTP and 1935 for RTMP.
* Path：The http file path for HTTP.
* App：The application for RTMP, similar to the directory of resource(stream).
* Stream：The stream for RTMP, similar to the resource(file) in specified app.

## NO Vhost

However, most user donot need the vhost, and it's a little complex, so donot use it when you donot need it. Most user actually only need app and stream.

When to use vhost? When you serve 100+ customers and use the same delivery network, they use theire own vhost and can use the sample app and stream.

The common use scenario, for example, if you use SRS to build a video website. So you are the only customer, donot need vhost. Suppose you provides video chat, there are some categories which includes some chat rooms. For example, categories are military, reader, history. The military category has rooms rock, radar; the reader category has red_masion. The config of SRS is very simple:

```bash
listen              1935;
vhost __defaultVhost__ {
}
```

When generate web pages, for instance, military category, all app is `military`. The url of chat room is `rtmp://yourdomain.com/military/rock`, while the encoder publish this stream, and all player play this stream.

The other pages of the military category use the same app name `military`, but use the different stream name, fr example, radar chat room use the stream url `rtmp://yourdomain.com/military/radar`.

When generate the pages, add new stream, the config of SRS no need to change. For example, when add a new chat room cannon, no need to change config of SRS. It is simple enough!

The reader category can use app `reader`, and the `red_mansion` chat room can use the url `rtmp://yourdomain.com/reader/red_mansion`.

## Vhost Use Scenarios

The vhost of RTMP is same to HTTP virtual server. For example, the demo.srs.com is resolve to 192.168.1.10 by dns or hosts:

| HTTP | Host | Port | Vhost |
| --- | --- | --- | ----- |
| http://demo.srs.com:80/players/srs_player.html | 192.168.1.10 | 80 | demo.srs.com |
| rtmp://demo.srs.com:1935/live/livestream | 192.168.1.10 | 1935 | demo.srs.com |

The use scenario of vhost:
* Multiple Customers: When need to serve multiple customers use the same network, for example, cctv and wasu delivery stream on the same CDN, how to seperate them, when they use the same app and stream?
* DNS scheduler: When CDN delivery content, the fast edge for the specified user is resolved for the dns name. We can use vhost as the dns name to scheduler user to different edge.
* Multiple Config sections: Sometimes we need different config, for example, to delivery RTMP for PC and transcode RTMP to HLS for android and IOS, we can use one vhost to delivery RTMP and another for HLS.

### Multiple Customers

For example, we got two customers cctv and wasu, use the same edge server 192.168.1.10, when user access the stream of these two customers:

| RTMP | Host | Port | Vhost | App | Stream |
| --- | --- | -------| ----- | ---| --------|
|  rtmp://show.cctv.cn/live/livestream | 192.168.1.10 | 1935 | show.cctv.cn | live | livestream |
|  rtmp://show.wasu.cn/live/livestream | 192.168.1.10 | 1935 | show.wasu.cn | live | livestream |

The config on the edge 192.168.1.10, need to config the vhost:

```bash
listen              1935;
vhost show.cctv.cn {
}
vhost show.wasu.cn {
}
```

### DNS GSLB

Please refer to the tech for DNS and CDN.

### Config Unit

For example, two customers cctv and wasu, and cctv needs mininum latency, while wasu needs fast startup. 

Then we config the cctv without gop cache, and wasu config with gop cache:

```bash
listen              1935;
vhost show.cctv.cn {
    chunk_size 128;
}
vhost show.wasu.cn {
    chunk_size 4096;
}
```

These two vhosts is completely isolated.

## Default Vhost

The default vhost is \_\_defaultVhost\_\ introduced by FMS. When mismatch and vhost not found, use the default vhost if configed.

For example, the config of SRS on 192.168.1.10:

```bash
listen              1935;
vhost demo.srs.com {
}
```

Then, when user access the vhost:
* rtmp://demo.srs.com/live/livestream：OK, matched vhost is demo.srs.com.
* rtmp://192.168.1.10/live/livestream：Failed, no matched vhost, and no default vhost.

The rule of default vhost is same to other vhost, the default is used for the vhost not matched and not find.

## Locate Vhost

There are two ways to access the vhost on server:
* DNS name: When access the dns name equals to the vhost, by dns resolve or hosts file, we can access the vhost on server.
* Stream parameters: While publishing or playing stream, the parameter can take the vhost. This needs the server supports this way, for example, SRS can use parameter `?vhost=VHOST` and `?domain=VHOST` to access specified vhost.

For example:

```bash
RTMP URL: rtmp://demo.srs.com/live/livestream
Edge servers: 50 servers
Edge server ip: 192.168.1.100 to 192.168.1.150
Edge SRS config:
    listen              1935;
    vhost demo.srs.com {
        mode remote;
        origin: xxxxxxx;
    }
```

The ways to access the url on edge servers:

| User   | RTMP URL | hosts                      | Target                   |
|--------| -------- |----------------------------|--------------------------|
| User   | rtmp://demo.srs.com/live/livestream | -                          | Resolved by DNS          |
| DevOps | rtmp://demo.srs.com/live/livestream | 192.168.1.100 demo.srs.com | Connect to 192.168.1.100 |
| DevOps     | rtmp://192.168.1.100/live?<br/>vhost=demo.srs.com/livestream | -                          | Connect to 192.168.1.100       |
| DevOps     | rtmp://192.168.1.100/live<br/>...vhost...demo.srs.com/livestream | -                          | Connect to 192.168.1.100       |

It is sample way to access other servers.

## Parameters in URL

There is no parameters for RTMP URL, similar to query string of HTTP, we can pass parameters in RTMP URL for SRS:
* Vhost：Specifies the vhost in the RTMP URL for SRS.
* Token authentication: Not implements for SRS, while user can specifies the token in the RTMP URL, SRS can fetch the token and verify it on remote authentication server. The token authentication is better and complex than refer authentication.

The parameters in SRS URL:

* `rtmp://192.168.1.100/live/livestream?vhost=demo.srs.com`
* `rtmp://192.168.1.100/live/livestream?domain=demo.srs.com`
* `rtmp://192.168.1.100/live/livestream?token=xxx`
* `rtmp://192.168.1.100/live/livestream?vhost=demo.srs.com&token=xxx`

> Note: FMLE passes the parameters in app, which should not be used now, because it confuses people.

It's also applied to other protocols, for example:

* `http://192.168.1.100/live/livestream.flv?vhost=demo.srs.com&token=xxx`
* `http://192.168.1.100/live/livestream.m3u8?vhost=demo.srs.com&token=xxx`
* `webrtc://192.168.1.100/live/livestream?vhost=demo.srs.com&token=xxx`

> Note: SRT is another story, please read [SRT Parameters](./srt.md) for details.

## URL of SRS

SRS always simplify the problem, never make it more complex.

The RTMP URL of SRS use standard RTMP URL. Generally do not need to modify the url or add parameters in it, except:
* Change vhost: Manually change vhost in RTMP URL for debuging.
* Token authentication: To support token authentication.

Furthermore, recomment user to use one level app and stream, never use multiple level app and stream. For example:

```bash
// Not recomment multiple level app and stream, which confuse people.
rtmp://demo.srs.com/show/live/livestream
rtmp://demo.srs.com/show/live/livestream/2013
```

The srs_player and srs_publisher donot support multiple level app and stream. Both srs_player and srs_publisher make the word behind the last / to stream, the left is tcUrl(vhost/app). For example:

```bash
// For both srs_player and srs_publisher:
// play or publish the following rtmp URL:
rtmp://demo.srs.com/show/live/livestream/2013
schema: rtmp
host/vhost: demo.srs.com
app: show/live/livestream
stream: 2013
```

It simplify the url, the palyer and publisher only need user to input a url, not tcUrl and stream.

The RTMP URL of SRS:

| URL | Description |
| ---- | ------ |
| rtmp://demo.srs.com/live/livestream | Standard RTMP URL |
| rtmp://192.168.1.10/live/livestream?vhost=demo.srs.com | URL specifies vhost |
| rtmp://demo.srs.com/live/livestream?key=ER892ID839KD9D0A1D87D | URL specifies token authentication |

## Example Vhosts in SRS

The full.conf of conf of SRS contains many vhost, which used to show each feature. All features is put into the vhost demo.srs.com:

| Category | Vhost | Description |
| -------- | ----- | ---- |
| RTMP | __defaultVhost__ | Default Vhost, only RTMP.| 
| RTMP | chunksize.vhost.com | Sample to set the chunk_size.| 
| Forward | same.vhost.forward.vhost.com | Sample for Foward stream to the same vhost.| 
| HLS | with-hls.vhost.com | Sample for HLS.| 
| HLS | no-hls.vhost.com | Sample to disable the HLS.| 
| RTMP | min.delay.com | Sample to config the minimum latency for RTMP.| 
| RTMP | refer.anti_suck.com | Sample for Refer anti-suck DRM.| 
| RTMP | removed.vhost.com | Sample to disable vhost.| 
| Callback | hooks.callback.vhost.com | Sample for http callback.| 
| Transcode | mirror.transcode.vhost.com | Sample for transcode, to use the sample filter of FFMPEG.| 
| Transcode | crop.transcode.vhost.com | Sample for transcode, to use the crop filter of FFMPEG.| 
| Transcode | logo.transcode.vhost.com | Sample for transcode, to use the logo filter of FFMPEG.| 
| Transcode | audio.transcode.vhost.com | Sample for transcode, to transcode audio only.| 
| Transcode | copy.transcode.vhost.com | Sample for transcode, demux and mux.| 
| Transcode | all.transcode.vhost.com | Sample for transcode, all transcode features.| 
| Transcode | ffempty.transcode.vhost.com | Sample for empty transcode, display the parameters.| 
| Transcode | app.transcode.vhost.com | Sample for transcode, transcode specified app streams.| 
| Transcode | stream.transcode.vhost.com | Sample for transcode, transcode specified streams. |

The demo.conf of conf of SRS, used for demo of SRS。

| Category | Vhost | Description |
| -------- | ----- | ---- |
| DEMO | players | The vhost for default stream of srs_player, ingest this stream.| 
| DEMO | players_pub | The vhost for the srs_publisher to publish stream to.| 
| DEMO | players_pub_rtmp | The low latency vhost for demo.| 
| DEMO | demo.srs.com | The full features for demo.| 
| Others | dev | The vhost for dev, ignore.|

Winlin 2014.10

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/rtmp-url-vhost)


