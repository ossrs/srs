---
title: RTSP
sidebar_label: RTSP
hide_title: false
hide_table_of_contents: false
---

# RTSP

RTSP is a well-established protocol with nearly 30 years of history. In the security surveillance industry, many companies have implemented their own RTSP servers, but they rarely open-source them and often add proprietary extensions. While RTMP servers are readily available, finding a good RTSP server is much more challenging.

SRS initially supported the RTSP protocol in version 2.0, but only for publishing streams (ANNOUNCE → SETUP → RECORD) without playback capabilities (DESCRIBE → SETUP → PLAY). In practice, many traditional client/server applications, decoders, embedded devices, and even the latest AI vision detection systems still prefer RTSP as their primary playback protocol.

This version reuses some protocol parsing code from version 2.0, removes the publishing functionality, adds playback support, and only works with TCP transport.

For more background, please refer to the [FAQ](../../../faq#rtsp).

## Usage

First, compile and start SRS (ensure you're using version `7.0.47+`):

```bash
cd srs/trunk && ./configure --rtsp=on && make
./objs/srs -c conf/rtsp.conf
```
> You must enable RTSP with `--rtsp=on` during compilation (disabled by default).

Then, publish a stream using RTMP:

```bash
ffmpeg -re -i doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

Finally, play the stream using RTSP (note: only TCP transport is supported):

```bash
ffplay -rtsp_transport tcp -i rtsp://localhost:8554/live/livestream
```

## Config

Reference `conf/rtsp.conf`:

```bash
rtsp_server {
    enabled on;
    listen 8554;
}

vhost __defaultVhost__ {
    rtsp {
        enabled on;
        rtmp_to_rtsp on;
    }
}
```

## Port

The RTSP protocol consists of two parts: signaling (DESCRIBE/SETUP/PLAY, etc.) and media transport (RTP/RTCP packets).

Signaling must use TCP protocol. The default port is `554`, but some systems require `root` privileges to listen on this port, so we've changed it to `8554`.

After successful signaling, media transport begins. There are two methods: TCP or UDP. TCP reuses the socket connection established during signaling. UDP transport is not supported because it requires port allocation. If you try to use UDP as transport, it will fail:

```bash
ffplay -rtsp_transport udp -i rtsp://localhost:8554/live/livestream

[rtsp @ 0x7fbc99a14880] method SETUP failed: 461 Unsupported Transport
rtsp://localhost:8554/live/livestream: Protocol not supported

[2025-07-05 21:30:52.738][WARN][14916][7d7gf623][35] RTSP: setup failed: code=2057
(RtspTransportNotSupported) : UDP transport not supported, only TCP/interleaved mode is supported
```

There are currently no plans to support UDP transport. In practice, UDP is rarely used; the vast majority of RTSP traffic uses TCP.

## RTP

When using TCP transport, each RTP/RTCP packet has an additional 4-byte header. The first byte is fixed at `0x24`, followed by a 1-byte channel identifier, followed by a 2-byte RTP packet length. See section 10.12 `Embedded (Interleaved) Binary Data` in `RFC2326`.

## Play Before Publish

RTSP supports audio with AAC and OPUS codecs, which is significantly different from RTMP or WebRTC.

RTSP uses commands to exchange SDP and specify the audio track to play, unlike WHEP or HTTP-FLV, which use the query string of the URL. RTSP depends on the player’s behavior, making it very difficult to use and describe.

Considering the feature that allows playing the stream before publishing it, it requires generating some default parameters in the SDP. For OPUS, the sample rate is 48 kHz with 2 channels, while AAC is more complex, especially regarding the sample rate, which may be 44.1 kHz, 32 kHz, or 48 kHz.

Therefore, for RTSP, we cannot support play-then-publish. Instead, there must already be a stream when playing it, so that the audio codec is determined.

## Opus Codec

No Opus codec support for RTSP, because for RTC2RTSP, it always converts RTC to RTMP frames, then converts them to RTSP packets. Therefore, the audio codec is always AAC after converting RTC to RTMP.

This means the bridge architecture needs some changes. We need a new bridge that binds to the target protocol. For example, RTC2RTMP converts the audio codec, but RTC2RTSP keeps the original audio codec.

Furthermore, the RTC2RTMP bridge should also support bypassing the Opus codec if we use enhanced-RTMP, which supports the Opus audio codec. I think it should be configurable to either transcode or bypass the audio codec. However, this is not relevant to RTSP.

## Architecture

For the RTSP protocol parsing, we copied code from version 3.0, removed SDP, RTP, and publishing-related code, keeping only the essential `SrsRtspRequest` and `SrsRtspResponse` for handling requests and responses. We only process five methods: `OPTIONS`, `DESCRIBE`, `SETUP`, `PLAY`, and `TEARDOWN`. This is sufficient for RTSP playback.

For the business logic, `SrsRtspConnection` handles client connections and protocol interactions, `SrsRtspPlayStream` consumes data from `SrsRtspSource`, `SrsRtspSource` manages multiple `SrsRtspConsumer` instances and distributes RTP packets, and finally `SrsRtspSendTrack` sends audio and video data to clients.

## Testing

### Unit Test

```bash
./configure --utest=on & make utest
./objs/srs_utest
```

### Regression Test

```bash
cd srs/trunk/3rdparty/srs-bench
go test ./srs -mod=vendor -v -count=1 -run=TestRtmpPublish_RtspPlay
```
> Note: You need to start SRS before running regression tests.

### BlackBox Test

```bash
cd srs/trunk/3rdparty/srs-bench
go test ./blackbox -mod=vendor -v -count=1 -run=TestFast_RtmpPublish_RtspPlay_Basic
```

## TODO

The current version implements only basic functionality. Additional features like authentication, redirection, and RTCP will be planned according to actual needs, possibly in the near future.

## IPv6

SRS (v7.0.67+) supports IPv6 for RTSP protocol, enabling dual-stack (IPv4/IPv6) operation for standards-based streaming. This allows RTSP clients to connect using IPv6 addresses while maintaining full compatibility with existing IPv4 infrastructure.

IPv6 support is enabled automatically when SRS detects IPv6 addresses in the configuration. Configure the RTSP server to listen on IPv6 addresses:

```bash
rtsp_server {
    enabled on;
    # Listen on both IPv4 and IPv6 for UDP media
    listen 8554 [::]:8554;
}

vhost __defaultVhost__ {
    rtsp {
        enabled on;
        rtmp_to_rtsp on;
    }
}
```

Play RTSP stream via IPv6 using FFplay (TCP transport only):

```bash
ffplay -rtsp_transport tcp -i 'rtsp://[::1]:8554/live/livestream'
```

Play RTSP stream via IPv6 using VLC:

```bash
vlc 'rtsp://[::1]:8554/live/livestream'
```

When using IPv6 addresses in RTSP URLs, the IPv6 address must be enclosed in square brackets:

```bash
# Local IPv6 loopback
rtsp://[::1]:8554/live/livestream

# Public IPv6 address
rtsp://[2001:db8::1]:8554/live/livestream

# With authentication (if implemented)
rtsp://user:pass@[2001:db8::1]:8554/live/livestream
```

SRS supports dual-stack RTSP operation, allowing both IPv4 and IPv6 clients to connect simultaneously:

```bash
rtsp_server {
    enabled on;
    # Listen on both IPv4 and IPv6
    listen 8554 [::]:8554;
}
```

This configuration allows:
- IPv4 clients: `rtsp://192.168.1.100:8554/live/livestream`
- IPv6 clients: `rtsp://[2001:db8::1]:8554/live/livestream`

## References

- [rfc2326-1998-rtsp.pdf](/files/rfc2326-1998-rtsp.pdf)