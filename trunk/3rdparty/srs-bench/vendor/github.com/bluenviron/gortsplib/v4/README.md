# gortsplib

[![Test](https://github.com/bluenviron/gortsplib/actions/workflows/test.yml/badge.svg)](https://github.com/bluenviron/gortsplib/actions/workflows/test.yml)
[![Lint](https://github.com/bluenviron/gortsplib/actions/workflows/lint.yml/badge.svg)](https://github.com/bluenviron/gortsplib/actions/workflows/lint.yml)
[![Go Report Card](https://goreportcard.com/badge/github.com/bluenviron/gortsplib)](https://goreportcard.com/report/github.com/bluenviron/gortsplib)
[![CodeCov](https://codecov.io/gh/bluenviron/gortsplib/branch/main/graph/badge.svg)](https://app.codecov.io/gh/bluenviron/gortsplib/tree/main)
[![PkgGoDev](https://pkg.go.dev/badge/github.com/bluenviron/gortsplib/v4)](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4#pkg-index)

RTSP 1.0 client and server library for the Go programming language, written for [MediaMTX](https://github.com/bluenviron/mediamtx).

Go &ge; 1.21 is required.

Features:

* Client
  * Query servers about available media streams
  * Read media streams from a server ("play")
    * Read streams with the UDP, UDP-multicast or TCP transport protocol
    * Read TLS-encrypted streams (TCP only)
    * Switch transport protocol automatically
    * Read selected media streams
    * Pause or seek without disconnecting from the server
    * Write to ONVIF back channels
    * Get PTS (relative) timestamp of incoming packets
    * Get NTP (absolute) timestamp of incoming packets
  * Write media streams to a server ("record")
    * Write streams with the UDP or TCP transport protocol
    * Write TLS-encrypted streams (TCP only)
    * Switch transport protocol automatically
    * Pause without disconnecting from the server
* Server
  * Handle requests from clients
  * Validate client credentials
  * Read media streams from clients ("record")
    * Read streams with the UDP or TCP transport protocol
    * Read TLS-encrypted streams (TCP only)
    * Get PTS (relative) timestamp of incoming packets
    * Get NTP (absolute) timestamp of incoming packets
  * Serve media streams to clients ("play")
    * Write streams with the UDP, UDP-multicast or TCP transport protocol
    * Write TLS-encrypted streams (TCP only)
    * Compute and provide SSRC, RTP-Info to clients
* Utilities
  * Parse RTSP elements
  * Encode/decode RTP packets into/from codec-specific frames

## Table of contents

* [Examples](#examples)
* [API Documentation](#api-documentation)
* [RTP Payload Formats](#rtp-payload-formats)
* [Specifications](#specifications)
* [Related projects](#related-projects)

## Examples

* [client-query](examples/client-query/main.go)
* [client-play](examples/client-play/main.go)
* [client-play-timestamp](examples/client-play-timestamp/main.go)
* [client-play-options](examples/client-play-options/main.go)
* [client-play-pause](examples/client-play-pause/main.go)
* [client-play-to-record](examples/client-play-to-record/main.go)
* [client-play-backchannel](examples/client-play-backchannel/main.go)
* [client-play-format-av1](examples/client-play-format-av1/main.go)
* [client-play-format-av1-to-jpeg](examples/client-play-format-av1-to-jpeg/main.go)
* [client-play-format-g711](examples/client-play-format-g711/main.go)
* [client-play-format-h264](examples/client-play-format-h264/main.go)
* [client-play-format-h264-to-jpeg](examples/client-play-format-h264-to-jpeg/main.go)
* [client-play-format-h264-to-disk](examples/client-play-format-h264-to-disk/main.go)
* [client-play-format-h264-mpeg4audio-to-disk](examples/client-play-format-h264-mpeg4audio-to-disk/main.go)
* [client-play-format-h265](examples/client-play-format-h265/main.go)
* [client-play-format-h265-to-jpeg](examples/client-play-format-h265-to-jpeg/main.go)
* [client-play-format-h265-to-disk](examples/client-play-format-h265-to-disk/main.go)
* [client-play-format-lpcm](examples/client-play-format-lpcm/main.go)
* [client-play-format-mjpeg](examples/client-play-format-mjpeg/main.go)
* [client-play-format-mpeg4audio](examples/client-play-format-mpeg4audio/main.go)
* [client-play-format-mpeg4audio-to-disk](examples/client-play-format-mpeg4audio-to-disk/main.go)
* [client-play-format-opus](examples/client-play-format-opus/main.go)
* [client-play-format-opus-to-disk](examples/client-play-format-opus-to-disk/main.go)
* [client-play-format-vp8](examples/client-play-format-vp8/main.go)
* [client-play-format-vp9](examples/client-play-format-vp9/main.go)
* [client-record-options](examples/client-record-options/main.go)
* [client-record-pause](examples/client-record-pause/main.go)
* [client-record-format-av1](examples/client-record-format-av1/main.go)
* [client-record-format-g711](examples/client-record-format-g711/main.go)
* [client-record-format-h264](examples/client-record-format-h264/main.go)
* [client-record-format-h264-from-disk](examples/client-record-format-h264-from-disk/main.go)
* [client-record-format-h265](examples/client-record-format-h265/main.go)
* [client-record-format-lpcm](examples/client-record-format-lpcm/main.go)
* [client-record-format-mjpeg](examples/client-record-format-mjpeg/main.go)
* [client-record-format-mpeg4audio](examples/client-record-format-mpeg4audio/main.go)
* [client-record-format-opus](examples/client-record-format-opus/main.go)
* [client-record-format-vp8](examples/client-record-format-vp8/main.go)
* [client-record-format-vp9](examples/client-record-format-vp9/main.go)
* [server](examples/server/main.go)
* [server-tls](examples/server-tls/main.go)
* [server-auth](examples/server-auth/main.go)
* [server-h264-to-disk](examples/server-h264-to-disk/main.go)
* [server-h264-from-disk](examples/server-h264-from-disk/main.go)
* [proxy](examples/proxy/main.go)

## API Documentation

[Click to open the API Documentation](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4#pkg-index)

## RTP Payload Formats

In RTSP, media streams are transmitted by using RTP packets, which are encoded in a specific, codec-dependent, format. This library supports formats for the following codecs:

### Video

|codec|documentation|encoder and decoder available|
|------|-------------|-----------------------------|
|AV1|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#AV1)|:heavy_check_mark:|
|VP9|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#VP9)|:heavy_check_mark:|
|VP8|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#VP8)|:heavy_check_mark:|
|H265|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#H265)|:heavy_check_mark:|
|H264|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#H264)|:heavy_check_mark:|
|MPEG-4 Video (H263, Xvid)|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#MPEG4Video)|:heavy_check_mark:|
|MPEG-1/2 Video|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#MPEG1Video)|:heavy_check_mark:|
|M-JPEG|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#MJPEG)|:heavy_check_mark:|

### Audio

|codec|documentation|encoder and decoder available|
|------|-------------|-----------------------------|
|Opus|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#Opus)|:heavy_check_mark:|
|Vorbis|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#Vorbis)||
|MPEG-4 Audio (AAC)|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#MPEG4Audio)|:heavy_check_mark:|
|MPEG-1/2 Audio (MP3)|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#MPEG1Audio)|:heavy_check_mark:|
|AC-3|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#AC3)|:heavy_check_mark:|
|Speex|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#Speex)||
|G726|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#G726)||
|G722|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#G722)|:heavy_check_mark:|
|G711 (PCMA, PCMU)|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#G711)|:heavy_check_mark:|
|LPCM|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#LPCM)|:heavy_check_mark:|

### Other

|codec|documentation|encoder and decoder available|
|------|-------------|-----------------------------|
|MPEG-TS|[link](https://pkg.go.dev/github.com/bluenviron/gortsplib/v4/pkg/format#MPEGTS)||

## Specifications

|name|area|
|----|----|
|[RFC2326, RTSP 1.0](https://datatracker.ietf.org/doc/html/rfc2326)|protocol|
|[RFC7826, RTSP 2.0](https://datatracker.ietf.org/doc/html/rfc7826)|protocol|
|[RFC8866, SDP: Session Description Protocol](https://datatracker.ietf.org/doc/html/rfc8866)|SDP|
|[RTP Payload Format For AV1 (v1.0)](https://aomediacodec.github.io/av1-rtp-spec/)|payload formats / AV1|
|[RTP Payload Format for VP9 Video](https://datatracker.ietf.org/doc/html/draft-ietf-payload-vp9-16)|payload formats / VP9|
|[RFC7741, RTP Payload Format for VP8 Video](https://datatracker.ietf.org/doc/html/rfc7741)|payload formats / VP8|
|[RFC7798, RTP Payload Format for High Efficiency Video Coding (HEVC)](https://datatracker.ietf.org/doc/html/rfc7798)|payload formats / H265|
|[RFC6184, RTP Payload Format for H.264 Video](https://datatracker.ietf.org/doc/html/rfc6184)|payload formats / H264|
|[RFC3640, RTP Payload Format for Transport of MPEG-4 Elementary Streams](https://datatracker.ietf.org/doc/html/rfc3640)|payload formats / MPEG-4 audio, MPEG-4 video|
|[RFC2250, RTP Payload Format for MPEG1/MPEG2 Video](https://datatracker.ietf.org/doc/html/rfc2250)|payload formats / MPEG-1 video, MPEG-2 audio, MPEG-TS|
|[RFC2435, RTP Payload Format for JPEG-compressed Video](https://datatracker.ietf.org/doc/html/rfc2435)|payload formats / M-JPEG|
|[RFC7587, RTP Payload Format for the Opus Speech and Audio Codec](https://datatracker.ietf.org/doc/html/rfc7587)|payload formats / Opus|
|[Multiopus in libwebrtc](https://webrtc-review.googlesource.com/c/src/+/129768)|payload formats / Opus|
|[RFC5215, RTP Payload Format for Vorbis Encoded Audio](https://datatracker.ietf.org/doc/html/rfc5215)|payload formats / Vorbis|
|[RFC4184, RTP Payload Format for AC-3 Audio](https://datatracker.ietf.org/doc/html/rfc4184)|payload formats / AC-3|
|[RFC6416, RTP Payload Format for MPEG-4 Audio/Visual Streams](https://datatracker.ietf.org/doc/html/rfc6416)|payload formats / MPEG-4 audio|
|[RFC5574, RTP Payload Format for the Speex Codec](https://datatracker.ietf.org/doc/html/rfc5574)|payload formats / Speex|
|[RFC3551, RTP Profile for Audio and Video Conferences with Minimal Control](https://datatracker.ietf.org/doc/html/rfc3551)|payload formats / G726, G722, G711, LPCM|
|[RFC3190, RTP Payload Format for 12-bit DAT Audio and 20- and 24-bit Linear Sampled Audio](https://datatracker.ietf.org/doc/html/rfc3190)|payload formats / LPCM|
|[Codec specifications](https://github.com/bluenviron/mediacommon#specifications)|codecs|
|[Golang project layout](https://github.com/golang-standards/project-layout)|project layout|

## Related projects

* [MediaMTX](https://github.com/bluenviron/mediamtx)
* [gohlslib](https://github.com/bluenviron/gohlslib)
* [mediacommon](https://github.com/bluenviron/mediacommon)
* [pion/sdp (SDP library used internally)](https://github.com/pion/sdp)
* [pion/rtp (RTP library used internally)](https://github.com/pion/rtp)
* [pion/rtcp (RTCP library used internally)](https://github.com/pion/rtcp)
