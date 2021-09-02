<h1 align="center">
  <a href="https://pion.ly"><img src="./.github/pion-gopher-webrtc.png" alt="Pion WebRTC" height="250px"></a>
  <br>
  Pion WebRTC
  <br>
</h1>
<h4 align="center">A pure Go implementation of the WebRTC API</h4>
<p align="center">
  <a href="https://pion.ly"><img src="https://img.shields.io/badge/pion-webrtc-gray.svg?longCache=true&colorB=brightgreen" alt="Pion webrtc"></a>
  <a href="https://sourcegraph.com/github.com/pion/webrtc?badge"><img src="https://sourcegraph.com/github.com/pion/webrtc/-/badge.svg" alt="Sourcegraph Widget"></a>
  <a href="https://pion.ly/slack"><img src="https://img.shields.io/badge/join-us%20on%20slack-gray.svg?longCache=true&logo=slack&colorB=brightgreen" alt="Slack Widget"></a>
  <a href="https://twitter.com/_pion?ref_src=twsrc%5Etfw"><img src="https://img.shields.io/twitter/url.svg?label=Follow%20%40_pion&style=social&url=https%3A%2F%2Ftwitter.com%2F_pion" alt="Twitter Widget"></a>
  <a href="https://github.com/pion/awesome-pion" alt="Awesome Pion"><img src="https://cdn.rawgit.com/sindresorhus/awesome/d7305f38d29fed78fa85652e3a63e154dd8e8829/media/badge.svg"></a>
  <br>
  <a href="https://travis-ci.org/pion/webrtc"><img src="https://travis-ci.org/pion/webrtc.svg?branch=master" alt="Build Status"></a>
  <a href="https://pkg.go.dev/github.com/pion/webrtc/v3"><img src="https://pkg.go.dev/badge/github.com/pion/webrtc/v3" alt="PkgGoDev"></a>
  <a href="https://codecov.io/gh/pion/webrtc"><img src="https://codecov.io/gh/pion/webrtc/branch/master/graph/badge.svg" alt="Coverage Status"></a>
  <a href="https://goreportcard.com/report/github.com/pion/webrtc"><img src="https://goreportcard.com/badge/github.com/pion/webrtc" alt="Go Report Card"></a>
  <a href="https://www.codacy.com/app/Sean-Der/webrtc"><img src="https://api.codacy.com/project/badge/Grade/18f4aec384894e6aac0b94effe51961d" alt="Codacy Badge"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
</p>
<br>

### New Release

Pion WebRTC v3.0.0 has been released! See the [release notes](https://github.com/pion/webrtc/wiki/Release-WebRTC@v3.0.0) to learn about new features and breaking changes.

If you aren't able to upgrade yet check the [tags](https://github.com/pion/webrtc/tags) for the latest `v2` release.

We would love your feedback! Please create GitHub issues or join [the Slack channel](https://pion.ly/slack) to follow development and speak with the maintainers.

----

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
* Full loss recovery and congestion control is not complete, see [pion/interceptor](https://github.com/pion/interceptor) for progress
  * See [ion](https://github.com/pion/ion-sfu/tree/master/pkg/buffer) for how an implementor can do it today

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

### Community
Pion has an active community on the [Slack](https://pion.ly/slack).

Follow the [Pion Twitter](https://twitter.com/_pion) for project updates and important WebRTC news.

We are always looking to support **your projects**. Please reach out if you have something to build!
If you need commercial support or don't want to use public methods you can contact us at [team@pion.ly](mailto:team@pion.ly)

### Contributing
Check out the **[contributing wiki](https://github.com/pion/webrtc/wiki/Contributing)** to join the group of amazing people making this project possible:

* [John Bradley](https://github.com/kc5nra) - *Original Author*
* [Michael Melvin Santry](https://github.com/santrym) - *Mascot*
* [Raphael Randschau](https://github.com/nicolai86) - *STUN*
* [Sean DuBois](https://github.com/Sean-Der) - *Original Author*
* [Michiel De Backker](https://github.com/backkem) - *SDP, Public API, Project Management*
* [Brendan Rius](https://github.com/brendanrius) - *Cleanup*
* [Konstantin Itskov](https://github.com/trivigy) - *SDP Parsing*
* [chenkaiC4](https://github.com/chenkaiC4) - *Fix GolangCI Linter*
* [Ronan J](https://github.com/ronanj) - *Fix STCP PPID*
* [wattanakorn495](https://github.com/wattanakorn495)
* [Max Hawkins](https://github.com/maxhawkins) - *RTCP*
* [Justin Okamoto](https://github.com/justinokamoto) - *Fix Docs*
* [leeoxiang](https://github.com/notedit) - *Implement Janus examples*
* [Denis](https://github.com/Hixon10) - *Adding docker-compose to pion-to-pion example*
* [earle](https://github.com/aguilEA) - *Generate DTLS fingerprint in Go*
* [Jake B](https://github.com/silbinarywolf) - *Fix Windows installation instructions*
* [Michael MacDonald](https://github.com/mjmac) - *Plan B compatibility, Remote TURN/Trickle-ICE, Logging framework*
* [Oleg Kovalov](https://github.com/cristaloleg) *Use wildcards instead of hardcoding travis-ci config*
* [Woodrow Douglass](https://github.com/wdouglass) *RTCP, RTP improvements, G.722 support, Bugfixes*
* [Tobias Fridén](https://github.com/tobiasfriden) *SRTP authentication verification*
* [Yutaka Takeda](https://github.com/enobufs) *Fix ICE connection timeout*
* [Hugo Arregui](https://github.com/hugoArregui) *Fix connection timeout*
* [Rob Deutsch](https://github.com/rob-deutsch) *RTPReceiver graceful shutdown*
* [Jin Lei](https://github.com/jinleileiking) - *SFU example use http*
* [Will Watson](https://github.com/wwatson) - *Enable gocritic*
* [Luke Curley](https://github.com/kixelated)
* [Antoine Baché](https://github.com/Antonito) - *OGG Opus export*
* [frank](https://github.com/feixiao) - *Building examples on OSX*
* [mxmCherry](https://github.com/mxmCherry)
* [Alex Browne](https://github.com/albrow) - *JavaScript/Wasm bindings*
* [adwpc](https://github.com/adwpc) - *SFU example with websocket*
* [imalic3](https://github.com/imalic3) - *SFU websocket example with datachannel broadcast*
* [Žiga Željko](https://github.com/zigazeljko)
* [Simonacca Fotokite](https://github.com/simonacca-fotokite)
* [Marouane](https://github.com/nindolabs) *Fix Offer bundle generation*
* [Christopher Fry](https://github.com/christopherfry)
* [Adam Kiss](https://github.com/masterada)
* [xsbchen](https://github.com/xsbchen)
* [Alex Harford](https://github.com/alexjh)
* [Aleksandr Razumov](https://github.com/ernado)
* [mchlrhw](https://github.com/mchlrhw)
* [AlexWoo(武杰)](https://github.com/AlexWoo) *Fix RemoteDescription parsing for certificate fingerprint*
* [Cecylia Bocovich](https://github.com/cohosh)
* [Slugalisk](https://github.com/slugalisk)
* [Agugua Kenechukwu](https://github.com/spaceCh1mp)
* [Ato Araki](https://github.com/atotto)
* [Rafael Viscarra](https://github.com/rviscarra)
* [Mike Coleman](https://github.com/fivebats)
* [Suhas Gaddam](https://github.com/suhasgaddam)
* [Atsushi Watanabe](https://github.com/at-wat)
* [Robert Eperjesi](https://github.com/epes)
* [Aaron France](https://github.com/AeroNotix)
* [Gareth Hayes](https://github.com/gazhayes)
* [Sebastian Waisbrot](https://github.com/seppo0010)
* [Masataka Hisasue](https://github.com/sylba2050) - *Fix Docs*
* [Hongchao Ma(马洪超)](https://github.com/hcm007)
* [Aaron France](https://github.com/AeroNotix)
* [Chris Hiszpanski](https://github.com/thinkski) - *Fix Answer bundle generation*
* [Vicken Simonian](https://github.com/vsimon)
* [Guilherme Souza](https://github.com/gqgs)
* [Andrew N. Shalaev](https://github.com/isqad)
* [David Hamilton](https://github.com/dihamilton)
* [Ilya Mayorov](https://github.com/faroyam)
* [Patrick Lange](https://github.com/langep)
* [cyannuk](https://github.com/cyannuk)
* [Lukas Herman](https://github.com/lherman-cs)
* [Konstantin Chugalinskiy](https://github.com/kchugalinskiy)
* [Bao Nguyen](https://github.com/sysbot)
* [Luke S](https://github.com/encounter)
* [Hendrik Hofstadt](https://github.com/hendrikhofstadt)
* [Clayton McCray](https://github.com/ClaytonMcCray)
* [lawl](https://github.com/lawl)
* [Jorropo](https://github.com/Jorropo)
* [Akil](https://github.com/akilude)
* [Quentin Renard](https://github.com/asticode)
* [opennota](https://github.com/opennota)
* [Simon Eisenmann](https://github.com/longsleep)
* [Ben Weitzman](https://github.com/benweitzman)
* [Masahiro Nakamura](https://github.com/tsuu32)
* [Tarrence van As](https://github.com/tarrencev)
* [Yuki Igarashi](https://github.com/bonprosoft)
* [Egon Elbre](https://github.com/egonelbre)
* [Jerko Steiner](https://github.com/jeremija)
* [Roman Romanenko](https://github.com/r-novel)
* [YongXin SHI](https://github.com/a-wing)
* [Magnus Wahlstrand](https://github.com/kyeett)
* [Chad Retz](https://github.com/cretz)
* [Simone Gotti](https://github.com/sgotti)
* [Cedric Fung](https://github.com/cedricfung)
* [Norman Rasmussen](https://github.com/normanr) - *Fix Empty DataChannel messages*
* [salmān aljammāz](https://github.com/saljam)
* [cnderrauber](https://github.com/cnderrauber)
* [Juliusz Chroboczek](https://github.com/jech)
* [John Berthels](https://github.com/jbert)
* [Somers Matthews](https://github.com/somersbmatthews)
* [Vitaliy F](https://github.com/funvit)
* [Ivan Egorov](https://github.com/vany-egorov)
* [Nick Mykins](https://github.com/nmyk)
* [Jason Brady](https://github.com/jbrady42)
* [krishna chiatanya](https://github.com/kittuov)
* [JacobZwang](https://github.com/JacobZwang)
* [박종훈](https://github.com/JonghunBok)
* [Sam Lancia](https://github.com/nerd2)
* [Henry](https://github.com/cryptix)
* [Jeff Tchang](https://github.com/tachang)
* [JooYoung Lim](https://github.com/DevRockstarZ)
* [Sidney San Martín](https://github.com/s4y)
* [soolaugust](https://github.com/soolaugust)
* [Kuzmin Vladimir](https://github.com/tekig)
* [Alessandro Ros](https://github.com/aler9)
* [Thomas Miller](https://github.com/tmiv)
* [yoko(q191201771)](https://github.com/q191201771)
* [Joshua Obasaju](https://github.com/obasajujoshua31)
* [Mission Liao](https://github.com/mission-liao)
* [Hanjun Kim](https://github.com/hallazzang)
* [ZHENK](https://github.com/scorpionknifes)
* [Rahul Nakre](https://github.com/rahulnakre)
* [OrlandoCo](https://github.com/OrlandoCo)
* [Assad Obaid](https://github.com/assadobaid)
* [Jamie Good](https://github.com/jamiegood) - *Bug fix in jsfiddle example*
* [Artur Shellunts](https://github.com/ashellunts)
* [Sean Knight](https://github.com/SeanKnight)
* [o0olele](https://github.com/o0olele)
* [Bo Shi](https://github.com/bshimc)
* [Suzuki Takeo](https://github.com/BambooTuna)
* [baiyufei](https://github.com/baiyufei)
* [pascal-ace](https://github.com/pascal-ace)
* [Threadnaught](https://github.com/Threadnaught)
* [Dean Eigenmann](https://github.com/decanus)
* [Cameron Elliott](https://github.com/cameronelliott)
* [Pascal Benoit](https://github.com/pascal-ace)
* [Mats](https://github.com/Mindgamesnl)
* [donotanswer](https://github.com/f-viktor)
* [Reese](https://github.com/figadore)
* [David Zhao](https://github.com/davidzhao)

### License
MIT License - see [LICENSE](LICENSE) for full text
