---
title: SRT
sidebar_label: SRT
hide_title: false
hide_table_of_contents: false
---

# SRT

SRT (Secure Reliable Transport) is a broadcasting protocol created by Haivision to replace RTMP. Many live streaming 
encoders like OBS, vMix, and FFmpeg already support SRT, and many users prefer it for streaming.

Adobe hasn't been updating the RTMP protocol or submitting it to standard organizations like RFC, so it doesn't support
many new features like HEVC or Opus. In March 2023, the Enhanced RTMP project was created, which now supports HEVC and 
AV1. SRS and OBS also support HEVC encoding based on Enhanced RTMP.

Since SRT uses TS encapsulation, it naturally supports new codecs. SRT is based on the UDP protocol, so it has lower 
latency and better performance on weak networks than RTMP. RTMP latency is usually 1-3 seconds, while SRT latency is 
300-500 milliseconds. SRT is more stable on weak networks, making it better for long-distance and outdoor broadcasting.

SRT is a core protocol of SRS. SRS has supported SRT since 2020 and improved its consistency with other core protocols
in 2022. SRT and RTMP have very high consistency in terms of callbacks and API support.

Please refer to [#1147](https://github.com/ossrs/srs/issues/1147) for the detailed research and development process.

## Usage

SRS has built-in support for SRT and can be used with [docker](./getting-started.md) or [compiled from source](./getting-started-build.md):

```bash
docker run --rm -it -p 1935:1935 -p 8080:8080 -p 10080:10080/udp ossrs/srs:5 \
  ./objs/srs -c conf/srt.conf
```

Use [FFmpeg(click to download)](https://ffmpeg.org/download.html) or [OBS(click to download)](https://obsproject.com/download) to push the stream:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish'
```

Open the following page to play the stream (if SRS is not on your local machine, replace localhost with the server IP):

* RTMP(VLC/ffplay): `rtmp://localhost/live/livestream`
* HLS by SRS player: [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html)
* SRT(ffplay): `srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=request`
* SRT(VLC): `srt://127.0.0.1:10080`

SRS supports converting SRT to other protocols, which will be described in detail below.

## Config

The configuration for SRT is as follows:

```bash
srt_server {
    # whether SRT server is enabled.
    # Overwrite by env SRS_SRT_SERVER_ENABLED
    # default: off
    enabled on;
    # The UDP listen endpoints for SRT, each with format as <[ip:]port>. The ip can be either ipv4 or ipv6,
    # or both. For example:
    #       listen 10080 [::]:10080 192.168.1.100:10080 10.10.10.100:10080;
    # Overwrite by env SRS_SRT_SERVER_LISTEN
    listen 10080;
    # For detail parameters, please read wiki:
    # @see https://ossrs.io/lts/en-us/docs/v7/doc/srt#config
    # The maxbw is the max bandwidth of the sender side.
    # 	-1: Means the biggest bandwidth is infinity.
    # 	 0: Means the bandwidth is determined by SRTO_INPUTBW.
    # 	>0: Means the bandwidth is the configuration value.
    # Overwrite by env SRS_SRT_SERVER_MAXBW
    # default: -1
    maxbw 1000000000;
    # Maximum Segment Size. Used for buffer allocation and rate calculation using packet counter assuming fully
    # filled packets. Each party can set its own MSS value independently. During a handshake the parties exchange
    # MSS values, and the lowest is used.
    # Overwrite by env SRS_SRT_SERVER_MSS
    # default: 1500
    mss 1500;
    # The timeout time of the SRT connection on the sender side in ms. When SRT connects to a peer costs time
    # more than this config, it will be close.
    # Overwrite by env SRS_SRT_SERVER_CONNECT_TIMEOUT
    # default: 3000
    connect_timeout 4000;
    # The timeout time of SRT connection on the receiver side in ms. When the SRT connection is idle
    # more than this config, it will be close.
    # Overwrite by env SRS_SRT_SERVER_PEER_IDLE_TIMEOUT
    # default: 10000
    peer_idle_timeout 8000;
    # Default app for vmix, see https://github.com/ossrs/srs/pull/1615
    # Overwrite by env SRS_SRT_SERVER_DEFAULT_APP
    # default: live
    default_app live;
    # Default mode for short streamid format (without #!:: prefix).
    # When client uses short streamid like "live/stream" or "stream", this config
    # determines whether it's a publisher or player by default.
    # Options: publish, request
    #   - publish: short streamid is treated as publisher
    #   - request: short streamid is treated as player (default)
    # Example: with default_mode=publish, "srt://host:port?streamid=live/stream" publishes.
    # Overwrite by env SRS_SRT_SERVER_DEFAULT_MODE
    # default: request
    default_mode request;
    # Default streamid when client doesn't provide one.
    # This is used when SRT client connects without setting SRTO_STREAMID socket option.
    # The streamid format follows SRT standard: #!::r=app/stream,m=publish|request
    # Overwrite by env SRS_SRT_SERVER_DEFAULT_STREAMID
    # default: #!::r=live/livestream,m=request
    default_streamid "#!::r=live/livestream,m=request";
    # The peerlatency is set by the sender side and will notify the receiver side.
    # Overwrite by env SRS_SRT_SERVER_PEERLATENCY
    # default: 0
    peerlatency 0;
    # The recvlatency means latency from sender to receiver.
    # Overwrite by env SRS_SRT_SERVER_RECVLATENCY
    # default: 120
    recvlatency 0;
    # This latency configuration configures both recvlatency and peerlatency to the same value.
    # Overwrite by env SRS_SRT_SERVER_LATENCY
    # default: 120
    latency 0;
    # The tsbpd mode means timestamp based packet delivery.
    # SRT sender side will pack timestamp in each packet. If this config is true,
    # the receiver will read the packet according to the timestamp in the head of the packet.
    # Overwrite by env SRS_SRT_SERVER_TSBPDMODE
    # default: on
    tsbpdmode off;
    # The tlpkdrop means too-late Packet Drop
    # SRT sender side will pack timestamp in each packet, When the network is congested,
    # the packet will drop if latency is bigger than the configuration in both sender side and receiver side.
    # And on the sender side, it also will be dropped because latency is bigger than configuration.
    # Overwrite by env SRS_SRT_SERVER_TLPKTDROP
    # default: on
    tlpktdrop off;
    # The send buffer size of SRT.
    # Overwrite by env SRS_SRT_SERVER_SENDBUF
    # default:  8192 * (1500-28)
    sendbuf 2000000;
    # The recv buffer size of SRT.
    # Overwrite by env SRS_SRT_SERVER_RECVBUF
    # default:  8192 * (1500-28)
    recvbuf 2000000;
    # The passphrase of SRT.
    # If passphrase is no empty, all the srt client must be using the correct passphrase to publish or play,
    # or the srt connection will reject. The length of passphrase must be in range 10~79.
    # @see https://github.com/Haivision/srt/blob/master/docs/API/API-socket-options.md#srto_passphrase.
    # Overwrite by env SRS_SRT_SERVER_PASSPHRASE
    # default: ""
    passphrase xxxxxxxxxxxx;
    # The pbkeylen of SRT.
    # The pbkeylen determined the AES encrypt algorithm, this option only allow 4 values which is 0, 16, 24, 32
    # @see https://github.com/Haivision/srt/blob/master/docs/API/API-socket-options.md#srto_pbkeylen.
    # Overwrite by env SRS_SRT_SERVER_PBKEYLEN
    # default: 0
    pbkeylen 16;
}
vhost __defaultVhost__ {
    srt {
        # Whether enable SRT on this vhost.
        # Overwrite by env SRS_VHOST_SRT_ENABLED for all vhosts.
        # Default: off
        enabled on;
        # Whether covert SRT to RTMP stream.
        # Overwrite by env SRS_VHOST_SRT_TO_RTMP for all vhosts.
        # Default: on
        srt_to_rtmp on;
    }
}
```

> Note: These configurations are for publish and play. Note that there are some other configurations in other sections,
for example, converting RTMP to [HTTP-FLV](./flv.md#config) or HTTP-TS.

All SRT configuration parameters can be found in the [libsrt](https://github.com/Haivision/srt/blob/master/docs/API/API-socket-options.md#list-of-options) documentation. Below are the important parameters supported by SRS:

* `tsbpdmode`: Timestamp-based packet delivery mode. Each packet gets a timestamp, and the application reads them at the interval specified by the timestamps.
* `latency`: In milliseconds (ms). This configures both recvlatency and peerlatency to the same value. If recvlatency is set, it will be used; if peerlatency is set, it will be used.
* `recvlatency`: In milliseconds (ms). This is the receiver's buffer time length, including the time it takes for a packet to travel from the sender, through the network, to the receiver, and finally to the media application. This buffer time should be greater than RTT and prepared for multiple packet retransmissions.
    * Low-latency networks: If the application requires low latency, consider setting the parameter to less than 250ms (human perception is not affected by audio/video latency below 250ms).
    * Long-distance, high RTT: If the transmission distance is long and RTT is high, a small latency cannot be set. For important live broadcasts that don't require low latency but need smooth playback without jitter, set latency >= 3*RTT, as this includes packet retransmission and ack/nack cycles.
* `peerlatency`: In milliseconds (ms). This is the sender's setting for peerlatency, telling the receiver how long the latency buffer should be. If the receiver also sets recvlatency, the receiver will use the larger of the two values as the latency buffer length.
    * Low-latency networks: Same recommendations as for `recvlatency`.
    * Long-distance, high RTT: Same recommendations as for `recvlatency`.
* `tlpkdrop`: Whether to drop too-late packets. Since SRT is designed for audio/video transmission, the receiver sends packets to the application based on timestamps or encoding bitrate. If a packet arrives too late at the receiver (after latency timeout), it will be dropped. In live mode, tlpkdrop is true by default, as live broadcasts require low latency.
* `maxbw`: In bytes/s, the maximum sending bandwidth. `-1`: Maximum bandwidth is 1Gbps; `0`: Determined by SRTO_INPUTBW calculation (not recommended for live mode); `>0`: Bandwidth in bytes/s.
* `mss`: In bytes, the maximum size of a single sent packet. This refers to the size of IP packets, including UDP and SRT protocol packets.
* `connect_timeout`: In milliseconds (ms), the SRT connection timeout.
* `peer_idle_timeout`: In milliseconds (ms), the SRT peer timeout.
* `sendbuf`: In bytes, the SRT send buffer size.
* `recvbuf`: In bytes, the SRT receive buffer size.
* `payloadsize`: In bytes, the payload size is a multiple of 188 (the minimum size of an MPEG-TS packet), defaulting to 1316 bytes (188x7).
* `passphrase`: The SRT connection password, default is empty (no encryption). The password must be between 10-79 characters long, and the client must enter the correct password to connect successfully, or the connection will be rejected.
* `pbkeylen`: The SRT encryption key length, default is 0. The stream encryption key length can be 0/16/24/32, corresponding to different AES encryption key lengths. This parameter needs to be set when the `passphrase` option is set.
* `srt_to_rtmp`: Whether to enable SRT to RTMP conversion. After converting to RTMP, it can be played using RTMP, HTTP-FLV, and HLS protocols.

## Low Latency Mode

If you want the lowest latency and can tolerate occasional packet loss, consider this setting.

> Note: Keep in mind that SRT will retransmit lost packets. Only when the network is very bad, and packets arrive very late or not at all, will they be discarded with `tlpktdrop` enabled, causing screen glitches.

For events, activities, and TV production with long-distance streaming, the link is usually prepared in advance and is stable and dedicated. In these scenarios, a fixed latency is required, allowing a certain degree of packet loss (very low probability). Generally, the RTT of the link is detected before the stream starts and is used as a basis for configuring SRT streaming parameters.

The recommended configuration is as follows, assuming an RTT of 100ms:

```bash
srt_server {
    enabled on;
    listen 10080;
    connect_timeout 4000;
    peerlatency 300; # RTT * 3
    recvlatency 300; # RTT * 3
    latency 300; # RTT * 3
    tlpktdrop on;
    tsbpdmode on;
}
```

This section describes how to reduce the latency of SRT, which is relevant to each link. The summary is as follows:

* Pay attention to the client's Ping and CPU, which are easily overlooked but can affect latency.
* Please use Oryx as the server, as it has been adjusted and will not cause additional latency.
* An increase in RTT will affect latency. Generally, with an RTT of below 60ms, it can be stable at the expected latency.
* With an RTT of 100ms, latency is approximately 300ms, and with an RTT of 150ms, latency increases to around 430ms.
* Packet loss will affect quality. With a packet loss rate of over 10%, there will be screen flickering and dropped frames, but it does not affect latency significantly, particularly for audio.
* Currently, the lowest latency can be achieved by using vmix or Xinxiang to stream SRT and playing it with ffplay, resulting in a latency of around 200ms.
* When streaming SRT with OBS and playing it with ffplay, the latency is around 350ms.

> Special Note: Based on current tests, the latency ceiling for SRT is 300ms. Although vmix can be set to a 1ms latency, it does not work and the actual latency will only be worse, not better. However, if the network is well maintained, a latency of 300ms is sufficient.

Recommended solution for ultra high-definition, ultra low-latency, SRT live streaming:

* Streaming: Xinxiang (230ms), vMix (200ms), OBS (300ms).
* Playback: ffplay (200ms), vMix (230ms), Xinxiang (400ms).

| - | ffplay | vMix Playback | Xinxiang Playback |
| ---           | ----      |  ---         | ---           |
| vMix Push | 200ms | 300ms | - |
| OBS Push | 300ms | - | - |
| Xinxiang Push (http://www.sinsam.com/) | 230ms | - | 400ms |

Latency involves each link, below are the detailed configurations for each link. The directory is as follows:

* [CPU](https://github.com/ossrs/srs/issues/3464#lagging-cpu) Client CPU can cause latency.
* [Ping](https://github.com/ossrs/srs/issues/3464#lagging-ping) Client network RTT affects latency.
* [Encoder](https://github.com/ossrs/srs/issues/3464#lagging-encoder) Configuring encoder for low latency mode.
* [Server](https://github.com/ossrs/srs/issues/3464#lagging-server) Configuring the server for low latency.
* [SRT](https://github.com/ossrs/srs/issues/3464#lagging-srt) Special configuration for SRT servers.
* [Player](https://github.com/ossrs/srs/issues/3464#lagging-player) Configuring the player for low latency.
* [Benchmark](https://github.com/ossrs/srs/issues/3464#lagging-benchmark) Accurately measuring latency.
* [Bitrate](https://github.com/ossrs/srs/issues/3464#lagging-bitrate) Impact of different bitrates (0.5 to 6Mbps) on latency.
* [Network Jitter](https://github.com/ossrs/srs/issues/3464#lagging-jitter) Impact of packet loss and different RTT on latency.
* [Report](https://github.com/ossrs/srs/issues/3464#lagging-report) Test report.

## High Quality Mode

If you want the highest quality and can't tolerate even a small chance of screen glitches, but can accept increased latency, consider this configuration.

When using SRT on public networks, the connection can be unstable, and RTT (Round Trip Time) may change dynamically. For low-latency live streaming, you need adaptive latency and must not lose packets.

Recommended settings are as follows:

```
srt_server {
    enabled on;
    listen 10080;
    connect_timeout 4000;
    peerlatency 0;
    recvlatency 0;
    latency 0;
    tlpktdrop off;
    tsbpdmode off;
}
```

> Note: If you still experience screen glitches with the above settings, please refer to the [FFmpeg patch](https://github.com/FFmpeg/FFmpeg/commit/9099046cc76c9e3bf02f62a237b4d444cdaf5b20).

## Video codec

Currently, H264 and HEVC encoding are supported. Since SRT protocol transfers media in MPEG-TS format, which already supports HEVC encoding (streamtype 0x24), SRT can naturally transmit HEVC encoded video without any modifications.

To stream with HEVC encoding, use the following command:
```bash
ffmpeg -re -i source.mp4 -c:v libx265 -c:a copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish'
```

To play HEVC encoded video, use the following command:
```bash
ffplay 'srt://127.0.0.1:10080?streamid=#!::h=live/livestream,m=request'
```

## Audio codec

Currently supported encoding formats:
* AAC, with sample rates of 44100, 22050, 11025, and 5512.

## FFmpeg push SRT stream

When using FFmpeg to push AAC audio format SRT stream, it is recommended to add the `-pes_payload_size 0` parameter in the command line. This parameter prevents multiple AAC audio frames from being combined into one PES package, reducing latency and audio-video synchronization issues.

FFmpeg command line example:

```bash
ffmpeg -re -i source.mp4 -c copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish'
```

## SRT URL

SRT URL uses YAML format, which is different from the common URL definition.

Consider the SRS definition for RTMP address, please refer to [RTMP URL](./rtmp-url-vhost.md) definition:

* Regular RTMP format (without vhost)
    - `rtmp://hostip:port/app/stream`
    - Example: `rtmp://10.111.1.100:1935/live/livestream`
    - In this example, app="live", stream="livestream"
* Complex RTMP format (with vhost)
    - `rtmp://hostip:port/app/stream?vhost=xxx`
    - Example: `rtmp://10.111.1.100:1935/live/livestream?vhost=srs.com.cn`
    - In this example, vhost="srs.com.cn", app="live", stream="livestream"

Whether it is streaming or playing, the RTMP address is a single address, and RTMP uses protocol layer messages to determine it. `publish message` means streaming to the URL, and `play message` means playing the URL.

SRT is a transport layer protocol, so it cannot determine whether the operation on an SRT URL is streaming or playing. The SRT documentation has recommendations for streaming/playing: [AccessControl.md](https://github.com/Haivision/srt/blob/master/docs/features/access-control.md)
The key method is to use the streamid parameter to clarify the purpose of the URL, and the streamid format complies with the YAML format.

Here is an SRT URL without vhost:
* Streaming address: `srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish`
* Playing address: `srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=request`
* Corresponding RTMP playing address: `rtmp://127.0.0.1/live/livestream`

Where:
* `#!::`, is the beginning, in line with the YAML format standard.
* `r`, maps to the `app/stream` in the RTMP address.
* `m`, `publish` means streaming, `request` means playing.

Here is an SRT URL with vhost support:
* Streaming address: `srt://127.0.0.1:10080?streamid=#!::h=srs.srt.com.cn,r=live/livestream,m=publish`
* Playing address: `srt://127.0.0.1:10080?streamid=#!::h=srs.srt.com.cn,r=live/livestream,m=request`
* Corresponding RTMP address: `rtmp://127.0.0.1/live/livestream?vhost=srs.srt.com.cn`

Where:
* `h`, maps to the vhost in the RTMP address

## SRT URL without streamid

Some devices do not support streamid input or do not support some special characters in streamid, such as `!`, `#`, `,`, etc. In this case, you can use only `ip:port` for streaming, such as `srt://127.0.0.1:10080`. For this URL, SRS will set the streamid to `#!::r=live/livestream,m=publish` by default.

In other words, the following two addresses are equivalent:
* `srt://127.0.0.1:10080`
* `srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish`

## SRT URL with short streamid

SRS supports a short streamid format without the `#!::` prefix for simpler URLs. When using short streamid format like `live/livestream` or just `livestream`, SRS uses the `default_mode` configuration to determine whether it's a publish or play request.

The `default_mode` configuration accepts two values:
* `publish`: Short streamid will be treated as publish/push mode.
* `request`: Short streamid will be treated as request/play/pull mode (this is the default).

For publisher-friendly setup where clients can push with simple URLs, configure `default_mode` to `publish`:

```bash
srt_server {
    enabled on;
    listen 10080;
    # Short streamid format will be treated as publisher
    default_mode publish;
}
```

With this configuration, you can publish with a simple URL:

```bash
# Publish with short streamid (uses default_mode=publish)
ffmpeg -re -i source.flv -c copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=live/livestream'

# Play with explicit mode (must specify m=request)
ffplay 'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=request'
```

For player-friendly setup where clients can play with simple URLs, use the default `default_mode=request`:

```bash
srt_server {
    enabled on;
    listen 10080;
    # Short streamid format will be treated as player (default)
    default_mode request;
}
```

With this configuration, you can play with a simple URL:

```bash
# Publish with explicit mode (must specify m=publish)
ffmpeg -re -i source.flv -c copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish'

# Play with short streamid (uses default_mode=request)
ffplay 'srt://127.0.0.1:10080?streamid=live/livestream'
```

SRS provides two ready-to-use configuration files:
* `conf/srt.shortstreamid.publish.conf`: For publisher-friendly setup with `default_mode publish`.
* `conf/srt.shortstreamid.play.conf`: For player-friendly setup with `default_mode request`.

Note that explicit mode specification with the full `#!::` prefix always takes precedence over `default_mode`. This allows clients that support full streamid format to override the default behavior.

## Authentication

For the definition of SRT URLs, please refer to [SRT URL Schema](#srt-url).

Here is a special note on how to include authentication information, see [SRS URL: Token](./rtmp-url-vhost.md#parameters-in-url). 
If you need to include authentication information such as the secret parameter, you can specify it in the streamid, for example:

```
streamid=#!::r=live/livestream,secret=xxx
```

Here is a specific example:

```
ffmpeg -re -i doc/source.flv -c copy -f mpegts \
    'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,secret=xxx,m=publish'
```

The address for forwarding to SRS would be like this:

```
rtmp://127.0.0.1:1935/live/livestream?secret=xxx
```

## SRT Encoder

SRT Encoder is an encoder based on the SRT adaptive bitrate. It predicts low-latency outbound bandwidth based on information such as RTT, maxBw, and inflight in the SRT protocol, dynamically adjusting the encoding bitrate to be based on the network outbound bandwidth.

GitHub address: [runner365/srt_encoder](https://github.com/runner365/srt_encoder)

Based on the basic congestion control algorithm of BBR, the encoder predicts the state machine of the encoding bitrate (keep, increase, decrease) based on the minRTT, maxBw, and current inflight within one cycle (1~2 seconds).

Note:
1) This example is just a basic BBR algorithm example, and users can implement the interfaces in the CongestionCtrlI class to improve the BBR algorithm.
2) SRT is still an evolving protocol, and the accuracy of its congestion control and external parameter updates is also improving.

Easy to use, after compiling, you can directly use the ffmpeg command line.

## Coroutine Native SRT

How does SRS implement SRT? Based on coroutine-based SRT architecture, we need to adapt it to ST as SRT has its own IO scheduling, so that we can achieve the best maintainability.

* For the specific code submission, please refer to [#3010](https://github.com/ossrs/srs/pull/3010) or [1af30dea](https://github.com/ossrs/srs/commit/1af30dea324d0f1729aabd22536ea62e03497d7d)

> Note: Please note that the SRT in SRS 4.0 is a non-ST architecture, and it is implemented by launching a separate thread, which may not have the same level of maintainability as the native ST coroutine architecture.

## IPv6

SRS (v7.0.67+) supports IPv6 for SRT protocol, enabling dual-stack (IPv4/IPv6) operation for low-latency streaming. This allows SRT clients to connect using IPv6 addresses while maintaining full compatibility with existing IPv4 infrastructure.

IPv6 support is enabled automatically when SRS detects IPv6 addresses in the configuration. Configure the SRT server to listen on IPv6 addresses:

```bash
srt_server {
    enabled on;
    # Listen on both IPv4 and IPv6
    listen 10080 [::]:10080;

    # Other SRT parameters remain the same
    maxbw 1000000000;
    mss 1500;
    connect_timeout 4000;
    peer_idle_timeout 8000;
    default_app live;
    peerlatency 0;
    recvlatency 0;
}
```

Publish SRT stream via IPv6 using FFmpeg:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -pes_payload_size 0 -f mpegts \
  'srt://[::1]:10080?streamid=#!::r=live/livestream,m=publish'
```

Play SRT stream via IPv6 using FFplay:

```bash
ffplay 'srt://[::1]:10080?streamid=#!::r=live/livestream,m=request'
```

When using IPv6 addresses in SRT URLs, the IPv6 address must be enclosed in square brackets:

```bash
# Publishing
srt://[2001:db8::1]:10080?streamid=#!::r=live/livestream,m=publish

# Playing
srt://[2001:db8::1]:10080?streamid=#!::r=live/livestream,m=request

# With vhost support
srt://[2001:db8::1]:10080?streamid=#!::h=srs.srt.com.cn,r=live/livestream,m=publish
```

SRS supports dual-stack SRT operation, allowing both IPv4 and IPv6 clients to connect simultaneously:

```bash
srt_server {
    enabled on;
    # Listen on both IPv4 and IPv6
    listen 10080 [::]:10080;
}
```

This configuration allows:
- IPv4 clients: `srt://192.168.1.100:10080?streamid=#!::r=live/stream,m=publish`
- IPv6 clients: `srt://[2001:db8::1]:10080?streamid=#!::r=live/stream,m=publish`

## VLC

VLC has an important limitation: it does not support the `streamid` URL parameter. When VLC connects 
to an SRT server, it always sends an empty `SRTO_STREAMID` socket option, regardless of what you put
in the URL. This means VLC can only use the simple URL format `srt://127.0.0.1:10080` without any 
streamid parameter.

To support VLC and other clients that don't set `SRTO_STREAMID`, SRS provides a `default_streamid` 
configuration option. When a client connects without setting streamid, SRS will use this configured 
default value. By default, SRS uses `#!::r=live/livestream,m=publish` for backward compatibility, 
but for VLC playback, you should configure it to use `m=request` mode instead.

SRS provides a ready-to-use configuration file `conf/srt.vlc.conf` optimized for VLC compatibility. 
Start SRS with this configuration:

```bash
./objs/srs -c conf/srt.vlc.conf
```

You can also set the default streamid using an environment variable, which is useful for Docker deployments:

```bash
env SRS_SRT_SERVER_DEFAULT_STREAMID="#!::r=live/livestream,m=request" \
    ./objs/srs -c conf/srt.conf
```

Here's a complete workflow example. First, publish a stream with FFmpeg (which explicitly sets streamid 
with `m=publish`):

```bash
ffmpeg -re -i ./doc/source.flv -c copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish'
```

Then play with VLC using the simple URL (VLC will use the server's default streamid with `m=request`):

- Open VLC Media Player
- Go to Media → Open Network Stream
- Enter URL: `srt://127.0.0.1:10080`
- Click Play

> Note: VLC doesn't support SRT with streamid, so you should use the simple URL format `srt://127.0.0.1:10080` without any streamid parameter.

You can also play with FFplay by explicitly setting the streamid:

```bash
ffplay 'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=request'
```

The key difference between clients: VLC always uses the server's `default_streamid` configuration, while 
FFmpeg/FFplay/OBS can set streamid in the URL or settings, which overrides the server default.

## Q&A

1. Does SRS support forwarding SRT streams to Nginx?

> Yes, it is supported. You can use OBS/FFmpeg to push SRT streams to SRS, and SRS will convert the SRT stream into the RTMP protocol. Then, you can convert RTMP to HLS, FLV, WebRTC, and also forward the RTMP stream to Nginx.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.net&path=/lts/doc/en/v7/srt)

