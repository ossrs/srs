<h1 align="center">
  <a href="https://pion.ly"><img src="./.github/pion-gopher-webrtc.png" alt="Pion WebRTC" height="250px"></a>
  <br>
  Pion WebRTC
  <br>
</h1>
<h4 align="center">A pure Go implementation of the WebRTC API</h4>
<p align="center">
  <a href="https://pion.ly"><img src="https://img.shields.io/badge/pion-webrtc-gray.svg?longCache=true&colorB=brightgreen" alt="Pion WebRTC"></a>
  <a href="https://sourcegraph.com/github.com/pion/webrtc?badge"><img src="https://sourcegraph.com/github.com/pion/webrtc/-/badge.svg" alt="Sourcegraph Widget"></a>
  <a href="https://pion.ly/slack"><img src="https://img.shields.io/badge/join-us%20on%20slack-gray.svg?longCache=true&logo=slack&colorB=brightgreen" alt="Slack Widget"></a>
  <a href="https://twitter.com/_pion?ref_src=twsrc%5Etfw"><img src="https://img.shields.io/twitter/url.svg?label=Follow%20%40_pion&style=social&url=https%3A%2F%2Ftwitter.com%2F_pion" alt="Twitter Widget"></a>
  <a href="https://github.com/pion/awesome-pion" alt="Awesome Pion"><img src="https://cdn.rawgit.com/sindresorhus/awesome/d7305f38d29fed78fa85652e3a63e154dd8e8829/media/badge.svg"></a>
  <br>
  <img alt="GitHub Workflow Status" src="https://img.shields.io/github/actions/workflow/status/pion/webrtc/test.yaml">
  <a href="https://pkg.go.dev/github.com/pion/webrtc/v3"><img src="https://pkg.go.dev/badge/github.com/pion/webrtc/v3.svg" alt="Go Reference"></a>
  <a href="https://codecov.io/gh/pion/webrtc"><img src="https://codecov.io/gh/pion/webrtc/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/webrtc/v3"><img src="https://goreportcard.com/badge/github.com/pion/webrtc/v3" alt="Go Report Card"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>
<br>

### Usage
[Go Modules](https://blog.golang.org/using-go-modules) are mandatory for using Pion WebRTC. So make sure you set `export GO111MODULE=on`, and explicitly specify `/v2` or `/v3` when importing.


**[example applications](examples/README.md)** contains code samples of common things people build with Pion WebRTC.

**[example-webrtc-applications](https://github.com/pion/example-webrtc-applications)** contains more full featured examples that use 3rd party libraries.

**[awesome-pion](https://github.com/pion/awesome-pion)** contains projects that have used Pion, and serve as real world examples of usage.

**[GoDoc](https://pkg.go.dev/github.com/pion/webrtc/v3)** is an auto generated API reference. All our Public APIs are commented.

**[FAQ](https://github.com/pion/webrtc/wiki/FAQ)** has answers to common questions. If you have a question not covered please ask in [Slack](https://pion.ly/slack) we are always looking to expand it.

Now go build something awesome! Here are some **ideas** to get your creative juices flowing:
* Send a video file to multiple browser in real time for perfectly synchronized movie watching.
* Send a webcam on an embedded device to your browser with no additional server required!
* Securely send data between two servers, without using pub/sub.
* Record your webcam and do special effects server side.
* Build a conferencing application that processes audio/video and make decisions off of it.
* Remotely control a robots and stream its cameras in realtime.

### Want to learn more about WebRTC?
Join our [Office Hours](https://github.com/pion/webrtc/wiki/OfficeHours). Come hang out, ask questions, get help debugging and
hear about the cool things being built with WebRTC. We also start every meeting with basic project planning.

Check out [WebRTC for the Curious](https://webrtcforthecurious.com). A book about WebRTC in depth, not just about the APIs.
Learn the full details of ICE, SCTP, DTLS, SRTP, and how they work together to make up the WebRTC stack.

This is also a great resource if you are trying to debug. Learn the tools of the trade and how to approach WebRTC issues.

This book is vendor agnostic and will not have any Pion specific information.

### Features
#### PeerConnection API
* Go implementation of [webrtc-pc](https://w3c.github.io/webrtc-pc/) and [webrtc-stats](https://www.w3.org/TR/webrtc-stats/)
* DataChannels
* Send/Receive audio and video
* Renegotiation
* Plan-B and Unified Plan
* [SettingEngine](https://pkg.go.dev/github.com/pion/webrtc/v3#SettingEngine) for Pion specific extensions


#### Connectivity
* Full ICE Agent
* ICE Restart
* Trickle ICE
* STUN
* TURN (UDP, TCP, DTLS and TLS)
* mDNS candidates

#### DataChannels
* Ordered/Unordered
* Lossy/Lossless

#### Media
* API with direct RTP/RTCP access
* Opus, PCM, H264, VP8 and VP9 packetizer
* API also allows developer to pass their own packetizer
* IVF, Ogg, H264 and Matroska provided for easy sending and saving
* [getUserMedia](https://github.com/pion/mediadevices) implementation (Requires Cgo)
* Easy integration with x264, libvpx, GStreamer and ffmpeg.
* [Simulcast](https://github.com/pion/webrtc/tree/master/examples/simulcast)
* [SVC](https://github.com/pion/rtp/blob/master/codecs/vp9_packet.go#L138)
* [NACK](https://github.com/pion/interceptor/pull/4)
* [Sender/Receiver Reports](https://github.com/pion/interceptor/tree/master/pkg/report)
* [Transport Wide Congestion Control Feedback](https://github.com/pion/interceptor/tree/master/pkg/twcc)
* [Bandwidth Estimation](https://github.com/pion/webrtc/tree/master/examples/bandwidth-estimation-from-disk)

#### Security
* TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 and TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA for DTLS v1.2
* SRTP_AEAD_AES_256_GCM and SRTP_AES128_CM_HMAC_SHA1_80 for SRTP
* Hardware acceleration available for GCM suites

#### Pure Go
* No Cgo usage
* Wide platform support
  * Windows, macOS, Linux, FreeBSD
  * iOS, Android
  * [WASM](https://github.com/pion/webrtc/wiki/WebAssembly-Development-and-Testing) see [examples](examples/README.md#webassembly)
  *  386, amd64, arm, mips, ppc64
* Easy to build *Numbers generated on Intel(R) Core(TM) i5-2520M CPU @ 2.50GHz*
  * **Time to build examples/play-from-disk** - 0.66s user 0.20s system 306% cpu 0.279 total
  * **Time to run entire test suite** - 25.60s user 9.40s system 45% cpu 1:16.69 total
* Tools to measure performance [provided](https://github.com/pion/rtsp-bench)

### Roadmap
The library is in active development, please refer to the [roadmap](https://github.com/pion/webrtc/issues/9) to track our major milestones.
We also maintain a list of [Big Ideas](https://github.com/pion/webrtc/wiki/Big-Ideas) these are things we want to build but don't have a clear plan or the resources yet.
If you are looking to get involved this is a great place to get started! We would also love to hear your ideas! Even if you can't implement it yourself, it could inspire others.

### Sponsoring
Work on Pion's congestion control and bandwidth estimation was funded through the [User-Operated Internet](https://nlnet.nl/useroperated/) fund, a fund established by [NLnet](https://nlnet.nl/) made possible by financial support from the [PKT Community](https://pkt.cash/)/[The Network Steward](https://pkt.cash/network-steward) and stichting [Technology Commons Trust](https://technologycommons.org/).

### Community
Pion has an active community on the [Slack](https://pion.ly/slack).

Follow the [Pion Twitter](https://twitter.com/_pion) for project updates and important WebRTC news.

We are always looking to support **your projects**. Please reach out if you have something to build!
If you need commercial support or don't want to use public methods you can contact us at [team@pion.ly](mailto:team@pion.ly)

### Contributing
Check out the [contributing wiki](https://github.com/pion/webrtc/wiki/Contributing) to join the group of amazing people making this project possible: [AUTHORS.txt](./AUTHORS.txt)

### License
MIT License - see [LICENSE](LICENSE) for full text
